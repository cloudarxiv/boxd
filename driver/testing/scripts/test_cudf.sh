#! /bin/env bash

die() {
    echo -e "\e[1;31mERROR: $1\e[0m" >&2
    exit 1
}

usage() {
  echo "./test_cudf.sh [-p | --primary] [-w | --watch-cmd]  [-h | --help]"
  echo "-p | --primary the primary command to run"
  echo "-w | --watch-cmd the monitoring command to run"
}

OTPS="p:w:h"
LONGOPTS="primary:,watch-cmd:,help" 
OPTIONS=$(getopt -o "$OTPS" --long "$LONGOPTS" -- "$@")

[ "$?" -ne "0" ] && usage >&2 && exit 1
eval set -- "$OPTIONS"

while true; do
  arg="$1"
  case "$arg" in
    -p | --primary)
      PRIMARY_CMD="$2"
      shift 2
      ;;
    -w | --watch-cmd)
      WATCH_CMD="$2"
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

if [ -z ${PRIMARY_CMD+x} ]; then
 die "Primary command not specified"
fi

if [ -z ${WATCH_CMD+x} ]; then
  die "Watch command not specified"
fi

# Output file for watch
WATCH_LOG="last_watch_output.log"

echo "Starting primary process: $PRIMARY_CMD"
# Execute the primary command in the background
eval "$PRIMARY_CMD" &
PRIMARY_PID=$!
echo "Primary process PID: $PRIMARY_PID"

if [ -z "$PRIMARY_PID" ]; then
  echo "Failed to get PID for primary process."
  exit 1
fi

echo "Starting watch on PID $PRIMARY_PID with command: $WATCH_CMD"
script -q -c "watch -n 1 '$WATCH_CMD'" /dev/null > "$WATCH_LOG" &
WATCH_PID=$!
echo "Watch process PID: $WATCH_PID"

# Wait for the primary process to complete
wait $PRIMARY_PID
PRIMARY_EXIT_CODE=$?
echo "Primary process (PID: $PRIMARY_PID) has exited with code: $PRIMARY_EXIT_CODE."

# Kill the watch process
if ps -p $WATCH_PID > /dev/null; then
   echo "Stopping watch process (PID: $WATCH_PID)..."
   kill $WATCH_PID
   # Wait a moment for watch to be killed
   sleep 0.5
   # Force kill if it's still alive
   if ps -p $WATCH_PID > /dev/null; then
       kill -9 $WATCH_PID
   fi
else
   echo "Watch process (PID: $WATCH_PID) already terminated."
fi

echo "The last output from watch has been saved to: $WATCH_LOG"
cat "$WATCH_LOG"
