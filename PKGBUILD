pkgname=capture-view
pkgver=0.1.0
pkgrel=1
pkgdesc='Low-latency HDMI USB capture-card viewer'
arch=('x86_64')
url='https://example.invalid/capture-view'
license=('MIT')
depends=('pipewire' 'sdl3' 'libjpeg-turbo' 'libglvnd' 'ffmpeg')
makedepends=('cmake' 'ninja' 'pkgconf')
source=()
sha256sums=()

build() {
  cmake -S "$startdir" -B "$srcdir/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr
  cmake --build "$srcdir/build"
}

package() {
  DESTDIR="$pkgdir" cmake --install "$srcdir/build"
  install -Dm644 "$startdir/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
