rm -f /tmp/display_daemon.sock
/home/wyb/virtual-drm/build/display_daemon &
DAEMON_PID=$!
sleep 0.3

LD_LIBRARY_PATH=/home/wyb/virtual-drm/build /home/wyb/virtual-drm/build/test_producer &
PRODUCER_PID=$!
sleep 0.5

LD_LIBRARY_PATH=/home/wyb/virtual-drm/build /home/wyb/virtual-drm/build/test_consumer &
CONSUMER_PID=$!
sleep 6

kill $PRODUCER_PID $CONSUMER_PID $DAEMON_PID 2>/dev/null
wait 2>/dev/null