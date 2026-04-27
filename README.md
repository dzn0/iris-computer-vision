<div align="center">

```
██╗██████╗ ██╗███████╗
██║██╔══██╗██║██╔════╝
██║██████╔╝██║███████╗
██║██╔══██╗██║╚════██║
██║██║  ██║██║███████║
╚═╝╚═╝  ╚═╝╚═╝╚══════╝
```

**Computer vision aim assist for Valorant**

![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-blue?style=flat-square)
![Language](https://img.shields.io/badge/language-C%2B%2B17-purple?style=flat-square)
![GPU](https://img.shields.io/badge/inference-DirectML-orange?style=flat-square)

</div>

---

Iris uses a YOLO26s model to detect players in real time and moves your mouse toward the closest target. Everything runs inside a single terminal window with a clean purple UI — no overlays, no second windows.

## Features

- **YOLO26s detection** via ONNX Runtime with DirectML (GPU acceleration on any DirectX 11 card)
- **DXGI screen capture** — low-latency, no performance impact on the game
- **Kernel-mode mouse driver** — movement bypasses Win32 entirely
- **Token-gated** — requires a valid key to launch
- **Auto-updating** — always downloads the latest version at startup, no manual updates needed
- **Interactive config** — tune every aiming parameter from the terminal before each session

## Getting Started

### 1. Prepare Windows

Iris requires a few system settings before it can run. Iris checks these automatically and tells you what to fix, but it's faster to do them beforehand.

**Disable Core Isolation (Memory Integrity)**
> Windows Security → Device Security → Core Isolation → Memory Integrity → Off

**Disable Windows Defender real-time protection**
> Windows Security → Virus & threat protection → Manage settings → Real-time protection → Off

Reboot after making these changes.

### 2. Get a token

Join the Discord (`@d.zn.`) and request access. You'll receive a token string to paste into Iris on first launch.

### 3. Run Iris

Download `Iris.exe` from the [latest release](https://github.com/dzn0/iris-computer-vision/releases/latest) and run it as **Administrator**.

Iris will:
1. Ask for your token and validate it
2. Check that all system requirements are met
3. Download the latest `IrisCore.exe` automatically
4. Show the configuration screen — adjust settings with arrow keys, press **Enter** to start

### 4. Launch Valorant

After pressing Enter on the config screen, Iris is active. Open Valorant normally.

> ⚠️ Always start Iris **before** opening Valorant.

### Controls

| Key | Action |
|-----|--------|
| `F1` | Toggle aim assist on / off |
| `Ctrl+C` | Stop Iris |

---

## Configuration

On every launch, before the detection starts, an interactive screen lets you tune all settings. Use **↑ ↓** to navigate rows, **← →** to adjust values. Changes are saved automatically for next time.

| Setting | Default | Description |
|---------|---------|-------------|
| Confidence Threshold | `0.65` | Minimum detection confidence to track |
| Priority | `Head` | Target preference: Body / Head / Body+Head |
| ROI Size | `960` | Capture region size in pixels (centered on screen) |
| Show Boxes | `Off` | Overlay detection boxes on screen |
| Autofire | `On` | Automatically shoot when on target |
| Autofire Initial Delay | `0.2s` | Delay before the first shot |
| Autofire Delay | `0.2s` | Delay between consecutive shots |
| Overshooting | `70` | Mouse movement strength factor |
| FOV Horizontal | `103°` | Your in-game horizontal FOV |
| FOV Vertical | `70°` | Your in-game vertical FOV |
| Sensitivity | `1.0` | Your in-game sensitivity |
| DPI | `800` | Your mouse DPI |
| Smoothing | `Off` | Movement damping (0 = instant) |
| Deadzone | `Off` | Minimum angle before movement is applied |
| Max Speed | `1600 px/s` | Maximum movement speed cap |

---

## Building from Source

> This is only needed if you want to modify the code. To just use Iris, grab the exe from [Releases](https://github.com/dzn0/iris-computer-vision/releases/latest).

### Prerequisites

- **Visual Studio 2022** with the *Desktop development with C++* workload
- **Windows SDK 10.0** (included with VS installer)
- Git (to clone the repo)

### Steps

**1. Clone the repository**
```bat
git clone https://github.com/dzn0/iris-computer-vision.git
cd iris-computer-vision
```

**2. Add private files**

Two files cannot be distributed publicly and must be sourced separately:

| File | Destination |
|------|-------------|
| `Driver.sys` | `IrisCore\rsc\Driver.sys` |
| `mapper.exe` | `IrisCore\rsc\mapper.exe` |

**3. Run the build script**
```bat
build.bat
```

`build.bat` will automatically:
- Download `deps.zip` (~69 MB) from GitHub Releases if `IrisCore\rsc\` is empty — contains all 44 runtime DLLs and the ONNX model
- Restore NuGet packages (ONNX Runtime + DirectML)
- Compile `Iris` and `IrisCore` in Release x64

Output: `x64\Iris.exe` and `x64\IrisCore.exe`

### Project structure

```
Iris/          # Launcher — token auth, system check, downloader
IrisCore/      # Engine — screen capture, YOLO inference, mouse driver
Driver/        # Kernel-mode mouse driver source
build.bat      # One-shot build script
```

---

## Requirements

- Windows 10 / 11 (64-bit)
- Administrator privileges
- Core Isolation (Memory Integrity) **disabled**
- Windows Defender real-time protection **disabled**
- Valorant / Vanguard **not running** at launch

---

## Credits

- **[TheCruZ/kdmapper](https://github.com/TheCruZ/kdmapper)** — kernel driver mapper used to load the mouse driver without a signed certificate

---

<div align="center">
<sub>dc: @d.zn.</sub>
</div>
