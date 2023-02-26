(use-modules (guix packages)
	     ((guix licenses) :prefix license:)
	     (zephyr build-system zephyr)
	     (zephyr packages zephyr)
	     (zephyr packages zephyr-xyz))

(package
  (name "k64f-temp-firmware")
  (version "0.0.0")
  (home-page "https://github.com/paperclip4465/guix-embedded-example")
  (source #f)
  (build-system zephyr-build-system)
  (arguments
   `(#:zephyr ,zephyr-3.1))
  (inputs (list hal-cmsis
		hal-nxp
		zcbor
		zephyr-mcuboot))
  (synopsis "Temperature measurement firmware for the k64f.")
  (description "Temperature measurement firmware for the k64f.")
  (license license:gpl3+))
