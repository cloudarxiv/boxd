#! /bin/bash -x

die() {
    echo -e "\e[1;31mERROR: $1\e[0m" >&2
    exit 1
}

usage(){
    echo "./make_gpu_work.sh [-e | --exec <executable>]  [-s | --soft_limit <limit>] [-d | --hard_limit <limit>] <-n | --name> [--] [arguments for executable...]"
    echo ""
    echo "  -e, --exec       : Path to the executable file."
    echo "  -s, --soft_limit : Soft GPU memory limit."
    echo "  -d, --hard_limit : Hard GPU memory limit."
    echo "  -n, --name       : Slice name"
    echo "  --               : Separator for arguments to be passed to the executable."
    echo "                     Any arguments after '--' will be passed directly to the executable."
    exit 1
}

check_prereq() {
    [ "$EUID" -ne "0" ] && die "This script needs root permissions"
}


OTPS="e:s:d:n:h"
LONGOPTS="exec:,soft_limit:,hard_limit:,name:,help" 
OPTIONS=$(getopt -o "$OTPS" --long "$LONGOPTS" -- "$@")

[ "$?" -ne "0" ] && usage >&2 && exit 1
eval set -- "$OPTIONS"

while true; do
  arg="$1"
  case "$arg" in
    -e | --exec)
      EXEC_FILE="$2"
      shift 2
      ;;
    -s | --soft_limit)
      SOFT_LIMIT="$2"
      shift 2
      ;;
    -d | --hard_limit)
      HARD_LIMIT="$2"
      shift 2
      ;;
    -n | --name)
      SLICE_NAME="$2"
      shift 2
      ;;
    -h | --help)
      usage
      shift 1 
      break
      ;;
    --) # End of options
      shift
      break
      ;;
    *) 
      echo "Internal error: unrecognized option '$1'" >&2
      exit 1
      ;;
  esac
done

EXEC_ARGS=("$@")

if [ -z ${EXEC_FILE+x} ]; then
  die "No executable given";
elif [ ! -f ${EXEC_FILE} ]; then
  die "Executable file doesn't exists!"
fi

if [ -z ${SOFT_LIMIT+x} ]; then
  die "No soft limit given";
fi
if [ -z ${HARD_LIMIT+x} ]; then
  die "No hard limit given";
  SLICE_NAME=$$
fi


PID=$$
if [ -z ${SLICE_NAME+x} ]; then
  echo "No name given using ${PID} ";
  SLICE_NAME="${PID}.slice"
fi


echo "EXEC_FILE: $EXEC_FILE"
echo "SOFT_LIMIT: $SOFT_LIMIT"
echo "HARD_LIMIT: $HARD_LIMIT"
echo "SLICE_NAME: ${SLICE_NAME}"


slice="/sys/fs/cgroup/${SLICE_NAME}"
sudo sh -c "echo '+uvm_ctrl' > /sys/fs/cgroup/cgroup.subtree_control" 
sudo mkdir -p "${slice}"
# sudo sh -c "echo 'strict' > ${slice}/gpu_mem.mode" 
sudo sh -c "echo ${PID} > ${slice}/cgroup.procs"
sudo sh -c "echo 0 ${SOFT_LIMIT} > ${slice}/uvm_ctrl.soft"
sudo sh -c "echo 1 ${SOFT_LIMIT} > ${slice}/uvm_ctrl.soft"
sudo sh -c "echo 0 ${HARD_LIMIT} > ${slice}/uvm_ctrl.hard"
sudo sh -c "echo 1 ${HARD_LIMIT} > ${slice}/uvm_ctrl.hard"

echo "Done ${slice}"
EXEC_FILE=`realpath ${EXEC_FILE}`
echo "Launching ${EXEC_FILE} ${EXEC_ARGS[@]} "
exec "${EXEC_FILE}" "${EXEC_ARGS[@]}"


