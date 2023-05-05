make app
for i in $(seq 0 500 4000); do
    start=`date +%s.%N`
    export UTHREADCT="$i"
    export WORKERCT="7"
    echo $UTHREADCT
    unset $UTHREADCT
    ./app
    end=`date +%s.%N`
    echo $( echo "$end - $start" | bc -l )
done

# export UTHREADCT="8000"
# export WORKERCT="7"
# start=`date +%s.%N`
# ./app
# end=`date +%s.%N`
# echo $( echo "$end - $start" | bc -l )

# start=`date +%s.%N`
# go run -gcflags='-N -l' test.go
# end=`date +%s.%N`
# echo $( echo "$end - $start" | bc -l )
#5.968430857