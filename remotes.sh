#!/usr/bin/env bash

set -e

function git_remote {
    if git remote | grep "^$1\$"
    then
        git remote set-url "$1" "$2"
    else
        git remote add "$1" "$2"
    fi
    git fetch "$1"
}

git_remote upstream https://github.com/torvalds/linux.git

for distro in bionic focal groovy hirsute
do
    git_remote "ubuntu-$distro" "git://git.launchpad.net/~ubuntu-kernel/ubuntu/+source/linux/+git/$distro"
done
