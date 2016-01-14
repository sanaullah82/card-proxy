#!/bin/sh

SELF="$0"
SERVANT=$(echo `basename $0` | sed 's/_restarter.sh//' | sed 's/-/_/')
BIN="$1"
PINGER="$2"
shift
shift
DELAY=2

cd /var/tmp

function log_info {
    DT=`date '+%Y-%m-%d %H:%M:%S'`
    MSG="$1"
    echo "[$DT] <$SELF> info: $MSG"
}

log_info "BIN=$BIN"
log_info "PINGER=$PINGER"

CFG_FILE="/etc/$SERVANT/$SERVANT.cfg.xml"
log_info "CFG_FILE=$CFG_FILE"

while [ 1 = 1 ] ; do
    export CONFIG_FILE="$CFG_FILE"
    "$BIN" "$@" &
    PID=$!
    while [ 1 = 1 ] ; do
        if kill -0 $PID > /dev/null 2> /dev/null ; then
            #log_info "Process \"$BIN\" ($PID) still exists."
            if ! "$PINGER" ; then
                log_info "Process \"$BIN\" ($PID) does not respond, trying again in 2 sec."
                sleep 2
                if ! "$PINGER" ; then
                    log_info "Process \"$BIN\" ($PID) is stalled, killing it."
                    kill -9 $PID > /dev/null 2> /dev/null
                fi
            else
                sleep 2
            fi
        else
            wait $PID
            RC=$?
            log_info "Process \"$BIN\" ($PID) exited with code $RC. Sleep for $DELAY sec before restart."
            break
        fi
    done
    sleep "$DELAY"
done

