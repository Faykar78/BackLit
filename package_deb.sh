#!/bin/bash
set -e
set -x # Enable debug logging

APP_NAME="backlit"
VERSION="1.0.0"
ARCH="amd64"
PKG_DIR="build_deb/${APP_NAME}_${VERSION}_${ARCH}"

echo "🔨 Building .deb package..."

# Compile everything
make clean || true  # Don't fail if nothing to clean

echo "Building kb_gui..."
make kb_gui || { echo "Failed to build kb_gui"; exit 1; }

echo "Building kb_ctl..."
make kb_ctl || { echo "Failed to build kb_ctl"; exit 1; }

echo "Building kb_service..."
make kb_service || { echo "Failed to build kb_service"; exit 1; }

# Create directory structure
mkdir -p "${PKG_DIR}/DEBIAN"
mkdir -p "${PKG_DIR}/usr/local/bin"
mkdir -p "${PKG_DIR}/usr/share/applications"
mkdir -p "${PKG_DIR}/etc/udev/rules.d"
mkdir -p "${PKG_DIR}/usr/share/backlit/assets"

# Copy control files
cp packaging/control "${PKG_DIR}/DEBIAN/"
cp packaging/postinst "${PKG_DIR}/DEBIAN/"
chmod 755 "${PKG_DIR}/DEBIAN/postinst"

# Copy binaries
cp kb_gui "${PKG_DIR}/usr/local/bin/"
cp kb_ctl "${PKG_DIR}/usr/local/bin/"
cp kb_service "${PKG_DIR}/usr/local/bin/"

# Copy system files
cp controlcenter.desktop "${PKG_DIR}/usr/share/applications/"
cp 99-keyboard-backlight.rules "${PKG_DIR}/etc/udev/rules.d/"

# Copy assets if any (currently none required by default, but structure is good)
# cp -r assets/* "${PKG_DIR}/usr/share/backlit/assets/"

# Build package
dpkg-deb --build "${PKG_DIR}"
mv "build_deb/${APP_NAME}_${VERSION}_${ARCH}.deb" .

echo "✅ Package created: ${APP_NAME}_${VERSION}_${ARCH}.deb"
