#!/bin/bash

aws lambda add-permission \
--function-name C2DS-keepalive-alarm \
--statement-id AlarmAction \
--action 'lambda:InvokeFunction' \
--principal lambda.alarms.cloudwatch.amazonaws.com \
--source-account 851725164311 \
--source-arn arn:aws:cloudwatch:us-east-2:851725164311:alarm:Coop\ Controller\ Offline \
--region us-east-2 \
--profile tennis@charliesfarm 