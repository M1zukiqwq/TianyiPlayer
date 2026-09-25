// Thin async wrapper around the SemiPlayer C ABI (see third_party/semi_player).
//
// Threading model:
//  - the UI thread posts commands (non-blocking) and polls events/results;
//  - a single reaper thread awaits command handles in posting order;
//  - the engine's video thread calls on_video_frame() which copies the RGBA
//    frame into a mailbox the UI consumes.
#pragma once

#include <semi_player/semi_player.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tianyi::player {

struct MediaInfo {
    std::int64_t duration_us = 0;
    int width = 0;
    int height = 0;
    bool has_audio = false;
    bool has_video = false;
};

// RGBA snapshot owned by the UI thread.
struct Frame {
    std::vector<std::uint8_t> pixels;
    int width = 0;
    int height = 0;
    int pitch = 0;
    std::int64_t pts_us = 0;
};

enum class Status {
    Idle,    // nothing open
    Opening, // open posted, awaiting result
    Ready,   // open + video output configured
    Playing,
    Paused,
    Ended,   // playback finished
    Error,
};

class Controller {
public:
    ~Controller();

    bool init();
    void shutdown();

    // Commands (non-blocking; results arrive on poll()).
    void open(const std::string &path);
    void play();
    void pause();
    void toggle_play();
    void seek_us(std::int64_t position_us);
    void seek_relative_us(std::int64_t delta_us);
    void close_media();

    // Drain engine events and finished commands; call once per UI frame.
    void poll();

    // Copy the newest video frame if it is newer than `last_seq`; returns true
    // when `out` was filled. `last_seq` is updated accordingly.
    bool consume_frame(Frame &out, std::uint64_t &last_seq);

    Status status() const;
    MediaInfo media() const;
    std::string current_path() const;
    std::string error_text() const;

    // Best-effort playhead: video PTS when frames flow, wall clock otherwise.
    std::int64_t position_us() const;

private:
    struct PendingCommand {
        semi_handle_t handle = 0;
        bool is_open = false;
        bool is_configure = false;
    };

    static void video_frame_trampoline(void *user_data, const semi_video_frame_t *frame);
    void on_video_frame(const semi_video_frame_t *frame);

    void post_open(const std::string &path);
    void post_configure();
    void reap_loop();
    void apply_open_result(const semi_command_result_t &result);

    // -- command plumbing --
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<PendingCommand> pending_;
    std::thread reaper_;
    bool running_ = true;

    // -- shared state --
    mutable std::mutex state_mutex_;
    Status status_ = Status::Idle;
    MediaInfo media_;
    std::string current_path_;
    std::string error_text_;

    // -- frame mailbox (engine thread -> UI thread) --
    std::mutex frame_mutex_;
    std::vector<std::uint8_t> frame_pixels_;
    int frame_width_ = 0;
    int frame_height_ = 0;
    int frame_pitch_ = 0;
    std::int64_t frame_pts_us_ = 0;
    std::uint64_t frame_seq_ = 0;

    // -- playhead estimation --
    mutable std::mutex clock_mutex_;
    std::int64_t anchor_media_us_ = 0;
    std::int64_t anchor_wall_us_ = 0;
    bool clock_running_ = false;
    std::int64_t frozen_media_us_ = 0;
};

} // namespace tianyi::player
