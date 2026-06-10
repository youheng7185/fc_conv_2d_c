# fc_conv_2d_c

```
make all
./conv2d
./fc
./softmax
```

There is memcmp in every file to check with the official tflite microspeech cpp testbench. The result from conv2d and fc is matches exactly as the official testbench, but softmax's result is slightly off due to multiplying with float.
