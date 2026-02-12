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
mkdir -p "${PKG_DIR}/usr/share/backlit/clevo-xsm-wmi"
mkdir -p "${PKG_DIR}/lib/systemd/system"

# Copy control files
cp packaging/control "${PKG_DIR}/DEBIAN/"
cp packaging/postinst "${PKG_DIR}/DEBIAN/"
chmod 755 "${PKG_DIR}/DEBIAN/postinst"

# Create prerm script for clean uninstallation
cat > "${PKG_DIR}/DEBIAN/prerm" << 'PRERM'
#!/bin/bash
set -e
MODULE_NAME="clevo-xsm-wmi"
MODULE_VERSION="1.0.0"

# Stop and disable service
systemctl stop clevo-xsm-wmi.service 2>/dev/null || true
systemctl disable clevo-xsm-wmi.service 2>/dev/null || true

# Unload module
rmmod clevo_xsm_wmi 2>/dev/null || true

# Remove DKMS module
dkms remove "${MODULE_NAME}/${MODULE_VERSION}" --all 2>/dev/null || true
rm -rf "/usr/src/${MODULE_NAME}-${MODULE_VERSION}"
PRERM
chmod 755 "${PKG_DIR}/DEBIAN/prerm"

# Copy binaries
cp kb_gui "${PKG_DIR}/usr/local/bin/"
cp kb_ctl "${PKG_DIR}/usr/local/bin/"
cp kb_service "${PKG_DIR}/usr/local/bin/"

# Copy system files
cp controlcenter.desktop "${PKG_DIR}/usr/share/applications/"
cp 99-keyboard-backlight.rules "${PKG_DIR}/etc/udev/rules.d/"

# Copy kernel module source + DKMS config (built on target via DKMS)
cp clevo-xsm-wmi/clevo-xsm-wmi.c "${PKG_DIR}/usr/share/backlit/clevo-xsm-wmi/"
cp clevo-xsm-wmi/Makefile "${PKG_DIR}/usr/share/backlit/clevo-xsm-wmi/"
cp clevo-xsm-wmi/dkms.conf "${PKG_DIR}/usr/share/backlit/clevo-xsm-wmi/"

# Copy systemd service for module autoload
cp clevo-xsm-wmi.service "${PKG_DIR}/lib/systemd/system/"

# Build package
dpkg-deb --build "${PKG_DIR}"
mv "build_deb/${APP_NAME}_${VERSION}_${ARCH}.deb" .

echo "✅ Package created: ${APP_NAME}_${VERSION}_${ARCH}.deb"

