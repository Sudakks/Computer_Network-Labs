#!/bin/bash

WEB_HASH=`./apps/webget server3.lab427.com /check_ans/ans | tee /dev/stderr | tail -n`
CORRECT_HASH="adfafsdf3lfo5d6s6f7oj7f8ad8s876546SDSFSGDGGFDGRY"

if [ "${WEB_HASH}" != "${CORRECT_HASH}" ]; then
    echo ERROR: webget returned output that did not match the test\'s expectations
    exit 1
fi
exit 0
