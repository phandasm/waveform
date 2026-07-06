## Changes in 1.9.0-beta2
- Build against OBS 32

## Changes in 1.9.0-beta1
- Hotfix for possible OOM crash
- Allow high and low cutoffs to be equal
- Improve render quality in waveform mode
- Allow much larger buffer sizes in waveform mode

## Installation
### Windows
Either  
- Use the installer and select your OBS folder.  
or  
- Extract Waveform\_v#.#.#\_Windows\_x86\_64.zip to the *root* of your OBS folder (e.g. `C:\Program Files\obs-studio`).

### Linux (Ubuntu 24.04, Flatpak)
<details>
<summary>Click for instructions</summary>

#### Prebuilt Binaries
- Download Waveform\_v#.#.#\_Ubuntu\_x86\_64.deb and install it using your distro's package manager (Debian/Ubuntu only).  

#### Flatpak
- `flatpak install flathub com.obsproject.Studio.Plugin.waveform`  

#### Source Build
- Step-by-step instructions in the [readme](https://github.com/phandasm/waveform/blob/master/README.md#linux).  

Note: Should work for most distros, but do not mix with the .deb package above.
</details>

### MacOS
<details>
<summary>Click for instructions</summary>

**MacOS binaries are not signed, you will need to manually unblock them.**

#### Intel Macs
- **Uninstall waveform versions prior to 1.6.0**
- Download Waveform\_v#.#.#\_MacOS\_x86\_64.pkg and run it.  

#### ARM64/Apple Silicon Macs
- Download Waveform\_v#.#.#\_MacOS\_arm64.pkg and run it.  
Note: Use the ARM version only if you have a native ARM build of OBS.
</details>
