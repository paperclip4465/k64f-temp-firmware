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
  (outputs '("out" "debug"))
  (arguments
   '(#:bin-name "k64f-temp-firmware"))
  (inputs (list zephyr-cmsis
		zephyr-hal-nxp
		zephyr-zcbor
		zephyr-mcuboot))
  (synopsis "Temperature measurement firmware for the k64f.")
  (description "Temperature measurement firmware for the k64f.")
  (license license:gpl3+))
