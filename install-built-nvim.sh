#!/usr/bin/env bash
set -euo pipefail

cd /home/charlesstein/personal/neovim

echo "Installing Neovim from:"
git log -1 --oneline --decorate
git describe --tags --exact-match 2>/dev/null || true
echo

sudo make install
hash -r || true

echo
echo "Installed nvim:"
type -a nvim
nvim --version | head -n 6
