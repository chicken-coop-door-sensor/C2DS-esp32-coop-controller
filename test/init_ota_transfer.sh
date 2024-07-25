export AWS_IOT_ENDPOINT_URL=https://ay1nsbhuqfhzk-ats.iot.us-east-2.amazonaws.com && \
export AWS_S3_URL=https://charlies-farm-ota.s3.us-east-2.amazonaws.com/coop-controller/v1.0.0-f6bc4f8/firmware.bin && \
aws iot-data publish \
    --endpoint-url $AWS_IOT_ENDPOINT_URL \
    --topic "coop/update/controller" \
    --cli-binary-format raw-in-base64-out \
    --payload "{\"controller\": \"${AWS_S3_URL}\"}" \
    --profile tennis@charliesfarm \
    --region us-east-2
