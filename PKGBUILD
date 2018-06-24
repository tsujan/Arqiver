# Maintainer: Pedram Pourang <tsujan2000@gmail.com>
pkgname=arqiver
pkgver=20180622
pkgrel=1
epoch=
pkgdesc="A Qt font-end for libarchive"
arch=('x86_64')
#url="https://github.com/lumina-desktop/lumina"
license=('BSD')
groups=('qt')
depends=('qt5-base>=5.7.1')
makedepends=()
checkdepends=()
optdepends=()
provides=()
conflicts=()
replaces=()
backup=()
options=()
install=
changelog=
#source=("$pkgname-$pkgver.tar.gz")
noextract=()
md5sums=()
validpgpkeys=()

build() {
	qmake ../$pkgname.pro
	make -j4 -l3
}

package() {
	make INSTALL_ROOT="$pkgdir/" install
}
