
sudo apt update && sudo apt upgrade
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt-get update
sudo apt install gcc-10
apt install g++-10
   24  update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-10 100
   25  update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-10 100
apt install cmake
