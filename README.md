# Waveform
![Linux CI](https://github.com/phandasm/waveform/actions/workflows/main.yml/badge.svg?event=push)  
Waveform is an audio spectral analysis plugin for [OBS Studio](https://obsproject.com/).  
It is based on [FFTW](https://www.fftw.org/) and optimized for AVX2/FMA3.  
![Screenshot](https://i.imgur.com/y40gfQB.png)

# Compiling
## Prerequisites
Clone the repo with submodules: `git clone --recurse-submodules`  
Or if you already cloned without them: `git submodule update --init --recursive`

## Windows
Waveform's only external dependency is libObs.  
You'll need to build obs-studio separately and point [CMake](https://cmake.org/) to it when building Waveform.  
From the CLI interface, the latter can be accomplished via `-DLibObs_ROOT="path/to/obs-studio"`.

## Linux (Ubuntu 20.04.3 LTS)
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
