#!/usr/bin/env bash
set -euo pipefail

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJ"

LINKER_FLAGS=""
if command -v mold >/dev/null 2>&1; then
    LINKER_FLAGS="-fuse-ld=mold"
fi

echo "=== [1/3] Compiling Horizon Roads 3D (horizonroads.plm) ==="
cmake -S "$PROJ" -B "${PROJ}/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    ${LINKER_FLAGS:+-DCMAKE_EXE_LINKER_FLAGS="$LINKER_FLAGS"}
cmake --build "${PROJ}/build" -j "${CMAKE_BUILD_PARALLEL_LEVEL:-3}"

echo "=== [2/3] Rendering 512x512 PNG icon from assets/icon.svg ==="
if [ ! -f "${PROJ}/click/assets/icon.png" ] && pkg-config --exists Qt5Gui Qt5Svg 2>/dev/null; then
    TMP_SVG="$(mktemp -d /tmp/svg2png.XXXXXX)"
    cat << 'EOF_CPP' > "${TMP_SVG}/svg2png.cpp"
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "minimal");
    QGuiApplication app(argc, argv);
    QSvgRenderer svg(QString::fromLocal8Bit(argv[1]));
    QImage img(512, 512, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    svg.render(&p);
    p.end();
    return img.save(QString::fromLocal8Bit(argv[2])) ? 0 : 1;
}
EOF_CPP
    g++ -O2 "${TMP_SVG}/svg2png.cpp" -o "${TMP_SVG}/svg2png" $(pkg-config --cflags --libs Qt5Gui Qt5Svg) -fPIC
    "${TMP_SVG}/svg2png" "${PROJ}/click/assets/icon.svg" "${PROJ}/click/assets/icon.png"
    mkdir -p "${PROJ}/release"
    cp -f "${PROJ}/click/assets/icon.png" "${PROJ}/release/icon-512.png"
    rm -rf "$TMP_SVG"
fi

echo "=== [3/3] Staging arm64 binary & LICENSE into Click tree ==="
ARCH_TRIPLET="$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || echo aarch64-linux-gnu)"
install -d -m 0755 "${PROJ}/click/lib/${ARCH_TRIPLET}/bin"
install -m 0755 "${PROJ}/build/horizonroads" "${PROJ}/click/lib/${ARCH_TRIPLET}/bin/horizonroads"
cp -f "${PROJ}/LICENSE" "${PROJ}/click/LICENSE"
ls -lh "${PROJ}/click/lib/${ARCH_TRIPLET}/bin/horizonroads" "${PROJ}/click/assets/icon.png"
echo "Horizon Roads 3D build complete!"
