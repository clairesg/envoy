source_venv() {
  VENV_DIR=$1
  if [[ "$VIRTUAL_ENV" == "" ]]; then
    if [[ -d "${VENV_DIR}"/venv ]]; then
      rm -r "${VENV_DIR}"/venv
    fi
    virtualenv "${VENV_DIR}"/venv --no-site-packages --python=python3
    source "${VENV_DIR}"/venv/bin/activate    
  else
    echo "Found existing virtualenv"
  fi
}
