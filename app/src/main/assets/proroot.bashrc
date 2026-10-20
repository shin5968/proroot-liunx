# App-managed interactive Bash startup. Preserve the user's own Bash settings.
if [ -f "$HOME/.bashrc" ]; then
    . "$HOME/.bashrc"
fi
export SHELL=/bin/bash
export USER=root
export LOGNAME=root
PS1='\u@localhost:\w\$ '
