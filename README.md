
# Zero-Copy H.264 Encoding Demo on Mainline Linux 5.19 for Allwinner V3s/S3

This demo program encodes frames from the DVP camera into H.264 NAL units, and is tested with mainline Linux (5.19.3). This demo is best used as a package within the [v3s3](https://github.com/Unturned3/v3s3) buildroot tree, since it relies on a few patches to the kernel to work.

> Allwinner S3 support is unverified, due to the lack of test hardware; however, it uses the same die as V3s, so I wouldn't expect any differences.

## Features

- Uses the latest mainline software (Linux v5.19) with minimum modifications. No dependency on Allwinner's old Linux 3.4 BSP kernel.
- Uses [cedar](https://github.com/aodzip/cedar/) and [libcedarc](https://github.com/aodzip/libcedarc) to interface with the CedarVE hardware video codec block. Unfortunately, some binary blobs are still used (in libcedarc).
- 1920x1080 @ 30FPS video encoding, tested on the LicheePi Zero development board with an OV5640 image sensor
- Zero-copy: frames captured by DVP camera is stored in a buffer shared with the video encoding engine (achieved by a hack detailed below)
- Efficient memory usage; CPU consumption during video encoding is nearly 0%.

## Zero-Copy Video Encoding

The proper way to achieve zero-copy would be to utilize `V4L2_DMABUF` or `V4L2_USERPTR` to share buffers. However, the former is unsupported by `libcedarc`, while the latter is unsupported by `sun6i-video`. This leaves us with `V4L2_MMAP`, but there is no way to obtain the physical addresses of the allocated buffers, which the video engine needs if we want to share our pre-allocated buffers with it.

This issue was resolved by a dirty hack: I added a custom ioctl() to the existing `sun6i-video` driver and borrowed some logic from deep within the `videobuf2` and `dma-buf` frameworks, which allowed me to find the DMA address (i.e. `dma_addr_t`) associated with the buffer allocated by `V4L2_MMAP` and convert it to a physical address (`phys_addr_t`) via `virt_to_phys()`. I'm 99% sure this is NOT the proper way to do this, but I've tested it many times and it _seems_ to work... If anyone knows a better way to achieve zero-copy video encoding on this platform, I'd love to hear about it.

