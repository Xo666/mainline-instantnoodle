source ../env.sh
make mrproper
make op8efi_defconfig
make -j$(nproc) rpm-pkg LOCALVERSION=
