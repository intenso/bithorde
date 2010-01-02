#!/bin/bash

cd $(dirname $0)
BINDIR="../.."
SERVER="$BINDIR/server"
BHUPLOAD="$BINDIR/bhupload"
BHGET="$BINDIR/bhget"
TESTFILE=testfile

function clean() {
    rm -rf cache? *.log "$TESTFILE"{,.new}
}

function setup() {
    mkdir cachea cacheb
    dd if=/dev/urandom of="$TESTFILE" bs=1024 count=1024
}

function verify() {
    if [ -e "$1" ]; then
        cmp "$1" $TESTFILE
    else
        return 1;
    fi
}

function verify_done() {
    [ ! -e "$1.idx" ]
}

function exit_error() {
    echo "ERROR: $1"
    exit 1
}

function exit_success() {
    clean
    echo "===== SUCCESS! ====="
    exit 1
}

function daemons_start() {
    clean && setup || exit_error "Failed setup"
    trap daemons_stop EXIT
    "$SERVER" a.config &> a.log &
    DAEMON1=$!
    "$SERVER" b.config &> b.log &
    DAEMON2=$!
    sleep 0.1
}

function daemons_stop() {
    for job in `jobs -p`; do
        disown $job && kill $job
    done
}

daemons_start
MAGNET=$("$BHUPLOAD" -u/tmp/bithorde-rta "$TESTFILE"|grep '^magnet:')
verify cachea/?????????????????????* || exit_error "Uploaded file did not match upload source"
verify_done cacheb/?????????????????????* || exit_error "Uploaded file still has an index, indicating not done"

"$BHGET" -u/tmp/bithorde-rtb -sy "$MAGNET" > "$TESTFILE.new"
verify_done cacheb/?????????????????????* || exit_error "Cached asset still has an index, indicating not done"
verify cacheb/?????????????????????* || exit_error "File wasn't cached properly"
verify "$TESTFILE.new" || exit_error "Downloaded file did not match upload source"

exit_success