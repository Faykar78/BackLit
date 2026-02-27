<div align="center">

# ✨ BackLit ✨

### 🌈 A Beautiful Keyboard Backlight Controller for Linux

<img src="https://img.shields.io/badge/GTK-4.0-blue?style=for-the-badge&logo=gtk" alt="GTK4">
<img src="https://img.shields.io/badge/C-00599C?style=for-the-badge&logo=c&logoColor=white" alt="C">
<img src="https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black" alt="Linux">

---

*Take control of your keyboard backlight with style* 🎨

</div>

## 🖼️ Features

| Feature | Description |
|---------|-------------|
| 🎡 **Color Wheel** | Beautiful circular color picker with 12 preset colors |
| 🔆 **Brightness Control** | Smooth 10-level brightness adjustment |
| 🌊 **Wave Effect** | Eye-catching animated wave with customizable color sequence |
| ⌨️ **Hotkey Support** | Ctrl+Numpad hotkeys work system-wide via xbindkeys |
| 🎨 **Modern UI** | Glassmorphism design with sleek animations |

## 📦 What's Inside

```
BackLit/
├── src/
│   ├── kb_gui.c       # Main GUI application
│   ├── kb_ctl.c       # CLI tool for scripting
│   └── kb_service.c   # Background hotkey daemon
├── kernel/
│   └── clevo-xsm-wmi/ # Kernel module (submodule)
├── install.sh         # One-click installer
└── 99-keyboard-backlight.rules  # udev permissions
```


## ⚠️ Important: Secure Boot

> [!CAUTION]
> **Secure Boot must be DISABLED** for this application to work!
> 
> The kernel module (`clevo-xsm-wmi`) cannot be loaded with Secure Boot enabled.
> Disable it in your BIOS/UEFI settings before installation.

## ⚡ Quick Install

```bash
git clone --recursive https://github.com/Faykar78/BackLit.git
cd BackLit
./install.sh
```

**That's it!** Launch "Keyboard Backlight" from your app menu 🚀

## 🛠️ Manual Build

### Dependencies

```bash
# Ubuntu/Debian
sudo apt install build-essential libgtk-4-dev linux-headers-$(uname -r)
```

### Build & Install

```bash
make all
sudo make install
```

## 🎮 Usage

### GUI Application
```bash
kb_gui
```

### CLI Tool
```bash
kb_ctl --status              # Show current settings
kb_ctl --color blue          # Set color
kb_ctl --brightness 5        # Set brightness (0-9)
kb_ctl --wave                # Toggle wave effect
```

### Hotkeys (Work Without App!)

Hotkeys use `xbindkeys` and work system-wide — no GUI needed.

| Key Combo | Action |
|-----------|--------|
| `Numpad *` | Toggle backlight ON/OFF |
| `Numpad +` | Increase brightness |
| `Numpad -` | Decrease brightness |
| `Numpad /` | Cycle color |

To install hotkeys:
```bash
./install_hotkeys.sh
```

## 🎨 Supported Colors

<div align="center">

🔵 Blue • 🩵 Cyan • 🟢 Green • 🟡 Lime • 🌕 Yellow • 🟠 Orange

🔴 Red • 💗 Pink • 🟣 Magenta • 💜 Purple • 🩶 Teal • ⚪ White

</div>

## 🔧 Supported Hardware

- **Clevo** laptops with RGB keyboard backlight
- **TUXEDO** laptops
- Other laptops using the Clevo EC interface

## 📄 License

MIT License - Feel free to use, modify, and share!

---

<div align="center">

**Made with ❤️ for the Linux community**

⭐ Star this repo if you find it useful!

</div>
