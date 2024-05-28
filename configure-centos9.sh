#!/bin/bash


cd ~cloud-user

dnf -y update
dnf -y install gcc g++ unzip glibc-static
dnf -y --enablerepo=crb install libstdc++-static
dnf -y remove docker docker-client docker-client-latest \
    docker-common docker-latest docker-latest-logrotate \
    docker-logrotate docker-engine
dnf -y install yum-utils wget cmake ninja-build

yum -y erase podman buildah
yum-config-manager \
    --add-repo https://download.docker.com/linux/centos/docker-ce.repo
yum -y install docker-ce docker-ce-cli containerd.io

systemctl enable --now docker

yum -y localinstall \
    https://download-ib01.fedoraproject.org/pub/epel/7/x86_64/Packages/c/czmq-3.0.2-3.el7.x86_64.rpm
    
#scl enable devtoolset-9 ./helper.sh
docker pull racelab/cspot-docker-centos7
docker tag racelab/cspot-docker-centos7 cspot-docker-centos7

if ! [[ "$LD_LIBRARY_PATH" == *"/usr/local/lib"* ]]; then
    echo -e "if ! [[ \$LD_LIBRARY_PATH == *\"/usr/local/lib\"* ]]; then\nexport  LD_LIBRARY_PATH=\"\$LD_LIBRARY_PATH:/usr/local/lib\"\nfi" \
     >> ~/.profile
fi

# cp ../SELF-TEST.sh ./bin
# cd ./bin
# ./SELF-TEST.sh
