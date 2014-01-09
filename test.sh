PROGNAME=$1
CONFIG_FILE=$2

PREPROC_RESULT=${CONFIG_FILE}_result.tmp
PREPROC_ERROR=${CONFIG_FILE}_error.tmp
POSTPROC_RESULT=${CONFIG_FILE}_result.txt
EXPECTED_RESULT=${CONFIG_FILE}.result
TMP_DIFF=${CONFIG_FILE}.diff
TIME_TO_KILL=1

. /etc/init.d/functions

echo -n $PROGNAME -c ${CONFIG_FILE}
./${PROGNAME} -c ${CONFIG_FILE} > ${PREPROC_RESULT} 2> ${PREPROC_ERROR} &  sleep 2 ; kill $!
grep timestamp ${PREPROC_RESULT} | cut -d',' -f2- > ${POSTPROC_RESULT}
diff ${POSTPROC_RESULT} ${EXPECTED_RESULT} 2>&1 > ${TMP_DIFF}
if [ $? -eq 0 ] 
then
  echo_success
  echo
  rm -f ${PREPROC_RESULT} ${POSTPROC_RESULT} ${TMP_DIFF} ${PREPROC_ERROR}
else
  echo_failure
  echo
  echo "Expected (${EXPECTED_RESULT}):"
  cat ${EXPECTED_RESULT}
  echo
  echo "Result (${POSTPROC_RESULT}):"
  cat ${POSTPROC_RESULT}
  echo "diff with expected (${TMP_DIFF}):"
  cat ${TMP_DIFF}
fi

