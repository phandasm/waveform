# Waveform
![Mac/Linux CI](https://github.com/phandasm/waveform/actions/workflows/main.yml/badge.svg?event=push)  
Waveform is an audio spectral analysis plugin for [OBS Studio](https://obsproject.com/).  
It is based on [FFTW](https://www.fftw.org/) and optimized for AVX2/FMA3.  
![Screenshot](https://i.imgur.com/y40gfQB.png)

# Compiling
## Prerequisites
Clone the repo with submodules: `git clone --recurse-submodules`  
Or if you already cloned without them: `git submodule update --init --recursive`

## Minimum Supported Compilers
- GCC 10.1
- Clang 11.0
- VS 2019 16.8

## CMake Options
`STATIC_RUNTIME` Static link the CRT, MSVC only. Default: OFF  
`EXTRA_OPTIMIZATIONS` Enable aggressive compiler optimizations (LTCG), MSVC only. Default: OFF  
`ENABLE_X86_SIMD` Enable runtime detection and dynamic dispatch for AVX. Default: ON  
`HAVE_OBS_PROP_ALPHA` Enable alpha in the color picker. May need to be disabled for very old OBS versions. Default: ON  
`PACKAGED_INSTALL` Use package manager friendly folder structure when installing, Linux only. Default: OFF  
`BUILTIN_FFTW` Build FFTW from source and static link. Default: forced with MSVC, otherwise OFF  
`STATIC_FFTW` Static link against system-provided FFTW. Default: OFF  
`MAKE_DEB` Make deb package for Debian/Ubuntu. Default: OFF  
`MAKE_BUNDLE` Make macOS bundle. Default: OFF

### Deprecated Options
`DISABLE_X86_SIMD` Use `ENABLE_X86_SIMD` instead.

## Windows
Waveform's only external dependency is libobs.  
You'll need to build obs-studio separately and point [CMake](https://cmake.org/) to it when building Waveform.  
From the CLI interface, the latter can be accomplished via `-DCMAKE_PREFIX_PATH="path/to/obs-studio/build"`.

## Linux
### Ubuntu 20.04
```bash
# install build tools
sudo apt-get install build-essential git cmake

# install dependencies
sudo apt-get install libobs-dev libfftw3-dev libfftw3-3

# clone repo
git clone --recurse-submodules https://github.com/phandasm/waveform.git

# build in subfolder
mkdir waveform/build
cd waveform/build

# build
cmake ..
make
make install
```

### Fedora 41
**Prerequisite:** OBS packages from RPM Fusion (see [OBS Wiki](https://obsproject.com/wiki/unofficial-linux-builds#fedora)).  

```bash
sudo dnf install @c-development git cmake obs-studio-devel fftw-devel

git clone --recurse-submodules https://github.com/phandasm/waveform.git
mkdir waveform/build
cd waveform/build

cmake ..
make
make install
```

# Special Installation
## Linux Flatpak

If you install OBS from Flatpak, you must install Waveform from Flatpak as well:
```
flatpak install flathub com.obsproject.Studio.Plugin.waveform
```


