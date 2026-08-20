#!/usr/bin/env bash

set -euo pipefail

if [[ ${EUID} -ne 0 ]]; then
    echo "Run this script with sudo:"
    echo "  sudo bash $0"
    exit 1
fi

if [[ ! -f /etc/os-release ]]; then
    echo "Unable to determine the Linux distribution."
    exit 1
fi

# shellcheck disable=SC1091
source /etc/os-release

install_debian_dependencies() {
    echo "Installing Debian-based dependencies..."

    apt-get update

    apt-get install -y \
        build-essential \
        pkg-config \
        git \
        ca-certificates \
        libssl-dev \
        libsqlcipher-dev \
        sqlcipher \
        libsoapysdr-dev \
        soapysdr-tools \
        soapysdr-module-hackrf \
        soapysdr-module-rtlsdr \
        libfftw3-dev \
        libsdl2-dev \
        libsdl2-ttf-dev \
        libsdl2-image-dev \
        libargon2-dev \
        python3 \
        python3-numpy \
        gnuradio
}

install_fedora_dependencies() {
    echo "Installing Fedora-based dependencies..."

    dnf install -y \
        gcc \
        gcc-c++ \
        make \
        pkgconf-pkg-config \
        git \
        ca-certificates \
        openssl-devel \
        sqlcipher-devel \
        SoapySDR-devel \
        SoapySDR-tools \
        fftw-devel \
        SDL2-devel \
        SDL2_ttf-devel \
        SDL2_image-devel \
        libargon2-devel \
        python3 \
        python3-numpy \
        gnuradio
}

distribution_id="${ID,,}"
distribution_like="${ID_LIKE:-}"
distribution_like="${distribution_like,,}"

case "${distribution_id}" in
    debian | ubuntu | linuxmint | pop | parrot | kali)
        install_debian_dependencies
        ;;

    fedora)
        install_fedora_dependencies
        ;;

    *)
        if [[ " ${distribution_like} " == *" debian "* ]] ||
           [[ " ${distribution_like} " == *" ubuntu "* ]]; then
            install_debian_dependencies

        elif [[ " ${distribution_like} " == *" fedora "* ]] ||
             [[ " ${distribution_like} " == *" rhel "* ]]; then
            install_fedora_dependencies

        else
            echo "Unsupported distribution: ${PRETTY_NAME:-${ID}}"
            echo "Detected ID: ${ID}"
            echo "Detected ID_LIKE: ${ID_LIKE:-none}"
            echo
            echo "Explicitly supported distributions:"
            echo "  - Debian"
            echo "  - Ubuntu"
            echo "  - Linux Mint"
            echo "  - Pop!_OS"
            echo "  - Parrot OS"
            echo "  - Kali Linux"
            echo "  - Fedora"
            exit 1
        fi
        ;;
esac

echo
echo "Verifying required development libraries..."

required_pkg_config_packages=(
    "sdl2"
    "SDL2_ttf"
    "SDL2_image"
    "fftw3"
    "SoapySDR"
    "openssl"
    "libargon2"
)

missing=0

for package in "${required_pkg_config_packages[@]}"; do
    if pkg-config --exists "${package}"; then
        version="$(pkg-config --modversion "${package}")"
        printf "  %-18s %s\n" "${package}" "${version}"
    else
        printf "  %-18s MISSING\n" "${package}"
        missing=1
    fi
done

if command -v sqlcipher >/dev/null 2>&1; then
    printf "  %-18s installed\n" "sqlcipher"
else
    printf "  %-18s MISSING\n" "sqlcipher"
    missing=1
fi

if command -v python3 >/dev/null 2>&1; then
    printf "  %-18s %s\n" "python3" "$(python3 --version 2>&1)"
else
    printf "  %-18s MISSING\n" "python3"
    missing=1
fi

if python3 -c "import numpy" >/dev/null 2>&1; then
    printf "  %-18s installed\n" "Python NumPy"
else
    printf "  %-18s MISSING\n" "Python NumPy"
    missing=1
fi

if python3 -c "import gnuradio" >/dev/null 2>&1; then
    printf "  %-18s installed\n" "GNU Radio Python"
else
    printf "  %-18s MISSING\n" "GNU Radio Python"
    missing=1
fi

if command -v gnuradio-config-info >/dev/null 2>&1; then
    printf "  %-18s %s\n" \
        "GNU Radio" \
        "$(gnuradio-config-info --version 2>/dev/null)"
else
    printf "  %-18s MISSING\n" "GNU Radio"
    missing=1
fi

if command -v SoapySDRUtil >/dev/null 2>&1; then
    printf "  %-18s installed\n" "SoapySDRUtil"
else
    printf "  %-18s MISSING\n" "SoapySDRUtil"
    missing=1
fi

echo

if [[ ${missing} -ne 0 ]]; then
    echo "One or more required dependencies could not be verified."
    exit 1
fi

echo "RetroSpectrum dependencies installed successfully."
echo
echo "Build RetroSpectrum with:"
echo '  make '
