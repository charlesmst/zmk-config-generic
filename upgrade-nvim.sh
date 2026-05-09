#!/usr/bin/env bash
set -euo pipefail

log() {
  printf '\n==> %s\n' "$*"
}

need_sudo() {
  if [[ ${EUID} -eq 0 ]]; then
    SUDO=()
  else
    SUDO=(sudo)
  fi
}

need_sudo

log "Detecting Ubuntu release"
codename="$(. /etc/os-release && printf '%s' "${VERSION_CODENAME:-}")"
if [[ -z "${codename}" ]]; then
  codename="$(lsb_release -cs)"
fi
printf 'Ubuntu codename: %s\n' "${codename}"

log "Current nvim on PATH"
if command -v nvim >/dev/null 2>&1; then
  command -v nvim
  nvim --version | head -n 3 || true
else
  printf 'nvim is not currently on PATH\n'
fi

log "Disabling old Neovim unstable PPA files if present"
for file in /etc/apt/sources.list.d/*neovim*unstable*.list; do
  if [[ -e "${file}" ]]; then
    printf 'Moving %s to %s.disabled\n' "${file}" "${file}"
    "${SUDO[@]}" mv "${file}" "${file}.disabled"
  fi
done

log "Enabling Neovim stable PPA for ${codename}"
stable_file="/etc/apt/sources.list.d/neovim-ppa-stable-${codename}.list"
printf 'deb https://ppa.launchpadcontent.net/neovim-ppa/stable/ubuntu %s main\n' "${codename}" \
  | "${SUDO[@]}" tee "${stable_file}" >/dev/null

log "Updating apt package lists"
if ! "${SUDO[@]}" apt -o Acquire::ForceIPv4=true update; then
  cat <<'EOF'

apt update failed.
If the error mentions NO_PUBKEY, run this and then rerun this script:

  sudo apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 9DBB0BE9366964F134855E2255F96FCF8231B6DD

EOF
  exit 1
fi

log "Checking Neovim package candidate"
apt-cache policy neovim neovim-runtime
candidate="$(apt-cache policy neovim | awk '/Candidate:/ {print $2}')"
if [[ -z "${candidate}" || "${candidate}" == "(none)" || "${candidate}" == "0.6.1-3" ]]; then
  cat <<'EOF'

The Neovim stable PPA did not load correctly, so apt only sees Ubuntu's old
jammy package. The most likely problem is network access to Launchpad:

  https://ppa.launchpadcontent.net/neovim-ppa/stable/ubuntu

Fix that connectivity first, then rerun this script. If you only want the old
Ubuntu package, run this instead:

  sudo apt install --allow-downgrades neovim=0.6.1-3 neovim-runtime=0.6.1-3

EOF
  exit 1
fi

log "Installing Neovim from apt"
"${SUDO[@]}" apt install --allow-downgrades -y neovim neovim-runtime

log "Checking installed package"
apt-cache policy neovim

if [[ -x /usr/local/bin/nvim ]]; then
  log "Moving old local /usr/local/bin/nvim out of the way"
  backup="/usr/local/bin/nvim.local-backup.$(date +%Y%m%d-%H%M%S)"
  "${SUDO[@]}" mv /usr/local/bin/nvim "${backup}"
  printf 'Moved old binary to %s\n' "${backup}"
fi

hash -r || true

log "Final nvim resolution"
type -a nvim || true
nvim --version | head -n 5 || true

log "Done"
