# TianyiPlayer · 洛天依主题播放器

以 [SemiPlayer](https://github.com/Semiruyi/SemiPlayer) 为播放内核、SDL3 绘制界面的
洛天依主题桌面播放器。UI 与播放内核解耦：内核以 git submodule 形式挂载，
通过 SemiPlayer 的 C ABI 异步驱动。

![theme](assets/theme/tianyi_poster.jpg)

## 特性

- 💙 洛天依主题界面：天依蓝配色、插画背景、圆角控制条、樱粉进度滑块
- 🎬 视频播放：RGBA 帧邮箱 + 纹理上传，画面按窗口等比缩放
- 🎵 音频媒体支持（以背景插画作为播放画面）
- ⏯ 播放 / 暂停、精准 Seek（拖动进度条）、快捷键
- 🖱 拖放文件到窗口即可播放，也支持命令行传入路径
- 🧩 播放内核 SemiPlayer 以 submodule 挂载，随时与上游同步

## 快捷键

| 按键 | 功能 |
|---|---|
| 空格 | 播放 / 暂停 |
| ← / → | 快退 / 快进 5 秒 |
| F11 | 切换全屏 |
| Esc | 退出 |

## 构建（Windows / MSYS2 UCRT64）

```sh
pacman -S --needed \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-spdlog \
  mingw-w64-ucrt-x86_64-ffmpeg \
  mingw-w64-ucrt-x86_64-miniaudio \
  mingw-w64-ucrt-x86_64-sdl3 \
  mingw-w64-ucrt-x86_64-sdl3-image \
  mingw-w64-ucrt-x86_64-sdl3-ttf
```

```sh
git clone --recurse-submodules https://github.com/M1zukiqwq/TianyiPlayer.git
cd TianyiPlayer
cmake -B build -G Ninja
cmake --build build
./build/bin/tianyi_player.exe path/to/media.mp4
```

若已克隆但未拉取 submodule：`git submodule update --init --recursive`。

## 使用

```sh
./build/bin/tianyi_player.exe                # 显示主题海报
./build/bin/tianyi_player.exe example.mp4    # 直接播放
```

或把媒体文件拖进窗口。

## 架构

```
┌──────────────────────────────────────────────┐
│ UI 线程（本项目）                              │
│  SDL3 渲染 · 主题绘制 · 拖放/快捷键            │
│      │ 命令（非阻塞）        ▲ 帧邮箱          │
│      ▼                       │                │
│ PlayerController ── reaper 线程（await 句柄）  │
└──────────┬───────────────────────────────────┘
           │ SemiPlayer C ABI（semi_player.h）
┌──────────▼───────────────────────────────────┐
│ third_party/semi_player（git submodule）       │
│  FFmpeg 解封装/解码 · miniaudio 输出 · 音视频同步│
└──────────────────────────────────────────────┘
```

- UI 线程只做渲染与输入，调用 C ABI 的 `open/play/pause/seek/close` 均立即返回句柄；
- `PlayerController` 内的 reaper 线程按投递顺序 `await` 句柄并更新状态；
- 视频帧回调只在回调期内有效，控制器将其拷入帧邮箱，UI 线程上传到流式纹理。

## 主题素材与版权

插画与字体的版权归各自作者所有，详见 [NOTICE.md](NOTICE.md)。
本项目为个人非商业的主题演示；若要重新分发，请自行替换为获得授权的素材。

## 许可证

GPL-3.0-or-later（与内核 SemiPlayer 一致），详见 [LICENSE](LICENSE)。
