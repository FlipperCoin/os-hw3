#Usage: ./test_multiclient.sh <num_of_requests> <sleep_time>
#Example: ./test_multiclient.sh 20 0.2
#num_of_requests is a required argument
#sleep_time is optional - default is 3 seconds

REQUESTS=$1
if [ -z "$2" ]
  then
    SLEEP_TIME=3
  else
    SLEEP_TIME=$2
fi
for i in $(eval echo {1..$REQUESTS})
  do
    ./client localhost 2000 output.cgi?$SLEEP_TIME &
  done
