#include "player/player_controller.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace tianyi::player {
namespace {

std::int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

Controller::~Controller() {
    shutdown();
}

bool Controller::init() {
    if (semi_player_init() != SEMI_OK) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = Status::Error;
        error_text_ = "semi_player_init failed";
        return false;
    }
    running_ = true;
    reaper_ = std::thread([this] { reap_loop(); });
    // configure_video_output is only accepted while the session is Idle and the
    // values are consumed by the *next* open, so set up the video output first.
    post_configure();
    return true;
}

void Controller::shutdown() {
    close_media();
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        running_ = false;
    }
    queue_cv_.notify_all();
    if (reaper_.joinable()) {
        reaper_.join();
    }
    semi_player_shutdown();
}

void Controller::open(const std::string &path) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = Status::Opening;
        current_path_ = path;
        error_text_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(clock_mutex_);
        clock_running_ = false;
        anchor_media_us_ = 0;
        frozen_media_us_ = 0;
    }
    post_open(path);
}

void Controller::play() {
    const Status s = status();
    if (s == Status::Playing || s == Status::Opening || s == Status::Idle) {
        return;
    }
    if (s == Status::Ended) {
        seek_us(0);
    }
    semi_handle_t handle = semi_player_play();
    if (handle != 0) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        pending_.push_back({handle, false, false});
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = Status::Playing;
        error_text_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(clock_mutex_);
        anchor_media_us_ = frozen_media_us_;
        anchor_wall_us_ = now_us();
        clock_running_ = true;
    }
}

void Controller::pause() {
    if (status() != Status::Playing) {
        return;
    }
    semi_handle_t handle = semi_player_pause();
    if (handle != 0) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        pending_.push_back({handle, false, false});
    }
    {
        std::lock_guard<std::mutex> lock(clock_mutex_);
        frozen_media_us_ = position_us();
        clock_running_ = false;
    }
    std::lock_guard<std::mutex> lock(state_mutex_);
    status_ = Status::Paused;
}

void Controller::toggle_play() {
    if (status() == Status::Playing) {
        pause();
    } else {
        play();
    }
}

void Controller::seek_us(std::int64_t position_us) {
    const MediaInfo info = media();
    position_us = std::clamp<std::int64_t>(position_us, 0,
                                           info.duration_us > 0 ? info.duration_us : position_us);
    semi_handle_t handle = semi_player_seek(position_us, SEMI_SEEK_MODE_ACCURATE);
    if (handle != 0) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        pending_.push_back({handle, false, false});
    }
    {
        std::lock_guard<std::mutex> lock(clock_mutex_);
        anchor_media_us_ = position_us;
        frozen_media_us_ = position_us;
        anchor_wall_us_ = now_us();
    }
    if (status() == Status::Ended) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = Status::Paused;
    }
}

void Controller::seek_relative_us(std::int64_t delta_us) {
    seek_us(position_us() + delta_us);
}

void Controller::close_media() {
    semi_handle_t handle = semi_player_close();
    if (handle != 0) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        pending_.push_back({handle, false, false});
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = Status::Idle;
    }
    {
        std::lock_guard<std::mutex> lock(clock_mutex_);
        clock_running_ = false;
        frozen_media_us_ = 0;
        anchor_media_us_ = 0;
    }
}

void Controller::poll() {
    semi_player_event_t event{};
    while (semi_player_poll_event(&event) == SEMI_OK &&
           event.type != SEMI_PLAYER_EVENT_NONE) {
        if (event.type == SEMI_PLAYER_EVENT_PLAYBACK_FINISHED) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            status_ = Status::Ended;
        }
        event = semi_player_event_t{};
    }
}

bool Controller::consume_frame(Frame &out, std::uint64_t &last_seq) {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    if (frame_seq_ == 0 || frame_seq_ == last_seq) {
        return false;
    }
    out.pixels = frame_pixels_;
    out.width = frame_width_;
    out.height = frame_height_;
    out.pitch = frame_pitch_;
    out.pts_us = frame_pts_us_;
    last_seq = frame_seq_;
    return true;
}

Status Controller::status() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return status_;
}

MediaInfo Controller::media() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return media_;
}

std::string Controller::current_path() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_path_;
}

std::string Controller::error_text() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return error_text_;
}

std::int64_t Controller::position_us() const {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (media_.has_video) {
            std::lock_guard<std::mutex> frame_lock(frame_mutex_);
            if (frame_seq_ > 0) {
                return frame_pts_us_;
            }
        }
    }
    std::lock_guard<std::mutex> lock(clock_mutex_);
    if (clock_running_) {
        return anchor_media_us_ + (now_us() - anchor_wall_us_);
    }
    return frozen_media_us_;
}

void Controller::video_frame_trampoline(void *user_data, const semi_video_frame_t *frame) {
    if (user_data && frame) {
        static_cast<Controller *>(user_data)->on_video_frame(frame);
    }
}

void Controller::on_video_frame(const semi_video_frame_t *frame) {
    if (frame->plane_count == 0 || frame->planes[0].data == nullptr) {
        return;
    }
    const int width = static_cast<int>(frame->width);
    const int height = static_cast<int>(frame->height);
    const int src_pitch = static_cast<int>(frame->planes[0].stride_bytes);
    const int row_bytes = width * 4; // RGBA8888
    if (width <= 0 || height <= 0 || src_pitch < row_bytes) {
        return;
    }

    std::lock_guard<std::mutex> lock(frame_mutex_);
    frame_pixels_.resize(static_cast<std::size_t>(row_bytes) * height);
    const auto *src = static_cast<const std::uint8_t *>(frame->planes[0].data);
    for (int y = 0; y < height; ++y) {
        std::memcpy(frame_pixels_.data() + static_cast<std::size_t>(y) * row_bytes,
                    src + static_cast<std::size_t>(y) * src_pitch, row_bytes);
    }
    frame_width_ = width;
    frame_height_ = height;
    frame_pitch_ = row_bytes;
    frame_pts_us_ = frame->has_pts ? frame->pts_us : frame_pts_us_;
    ++frame_seq_;
}

void Controller::post_open(const std::string &path) {
    semi_handle_t handle = semi_player_open(path.c_str());
    if (handle == 0) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = Status::Error;
        error_text_ = "semi_player_open returned no handle";
        return;
    }
    std::lock_guard<std::mutex> lock(queue_mutex_);
    pending_.push_back({handle, true, false});
}

void Controller::post_configure() {
    semi_video_output_config_t config{};
    config.struct_size = sizeof(config);
    config.pixel_format = SEMI_VIDEO_PIXEL_FORMAT_RGBA8888;
    // The engine scales to this size; the UI fits the result into its window.
    config.output_width = kOutputWidth;
    config.output_height = kOutputHeight;
    config.on_frame = &Controller::video_frame_trampoline;
    config.user_data = this;

    semi_handle_t handle = semi_player_configure_video_output(&config);
    if (handle == 0) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = Status::Error;
        error_text_ = "configure_video_output returned no handle";
        return;
    }
    std::lock_guard<std::mutex> lock(queue_mutex_);
    pending_.push_back({handle, false, true});
}

void Controller::reap_loop() {
    for (;;) {
        PendingCommand cmd;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return !pending_.empty() || !running_; });
            if (pending_.empty()) {
                if (!running_) {
                    return;
                }
                continue;
            }
            cmd = pending_.front();
            pending_.pop_front();
        }

        semi_command_result_t result{};
        const int rc = semi_player_handle_await(cmd.handle, &result);

        if (cmd.is_open) {
            if (rc == SEMI_OK && result.has_media_info) {
                apply_open_result(result);
                std::lock_guard<std::mutex> lock(state_mutex_);
                if (status_ == Status::Opening) {
                    status_ = Status::Ready;
                }
            } else {
                std::lock_guard<std::mutex> lock(state_mutex_);
                status_ = Status::Error;
                error_text_ = (rc == SEMI_OK) ? "open returned no media info"
                                              : ("open failed, status=" + std::to_string(rc));
            }
        } else if (cmd.is_configure) {
            if (rc != SEMI_OK) {
                std::lock_guard<std::mutex> lock(state_mutex_);
                status_ = Status::Error;
                error_text_ = "configure_video_output failed, status=" + std::to_string(rc);
            }
        }
    }
}

void Controller::apply_open_result(const semi_command_result_t &result) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    media_.duration_us = result.media_info.duration_us;
    media_.width = static_cast<int>(result.media_info.video_width);
    media_.height = static_cast<int>(result.media_info.video_height);
    media_.has_audio = result.media_info.has_audio != 0;
    media_.has_video = result.media_info.has_video != 0;
}

} // namespace tianyi::player
