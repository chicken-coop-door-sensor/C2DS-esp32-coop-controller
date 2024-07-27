export AWS_IOT_ENDPOINT_URL=https://ay1nsbhuqfhzk-ats.iot.us-east-2.amazonaws.com && \
aws iot-data publish \
    --endpoint-url $AWS_IOT_ENDPOINT_URL \
    --topic "coop/door/state" \
    --cli-binary-format raw-in-base64-out \
    --payload "{\"door\": \"CLOSED\"}" \
    --profile tennis@charliesfarm \
    --region us-east-2
