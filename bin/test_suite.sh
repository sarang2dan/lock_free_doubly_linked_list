#!/bin/sh


function tf_sleep() {
  if [[ ! $# -eq 1 ]]; then
    echo "${FUNCNAME[0]} <sleep_interval>"
    exit;
  fi
  perl -e "select(undef,undef,undef,$1);"
} 

function echo_stage() {
  let stage+=1;
  printf "\
##############################################################################\n\
 ${stage}. $1
##############################################################################\n"
}

function exec_cmd() {
  echo $*
  $TIMECMD $*
}

if [[ -z $PRINT_ELAPSED_TIME ]] ; then
  TIMECMD=
else
  TIMECMD="time -p"
fi

##############################################################################
echo_stage "INFORMATION - evictor thr: 1, ager thr: 1 (FIXED)";
##############################################################################

##############################################################################
echo_stage "basic test - item #: 5,000,000, insert thr: 5, read thr: 10,";
##############################################################################
exec_cmd lf_dlist_test --item-count=5000000 --num-thr-insert=5 --num-thr-read=10

