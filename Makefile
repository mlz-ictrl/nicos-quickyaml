release-patch:
	MODE="patch" $(MAKE) release

release-minor:
	MODE="minor" $(MAKE) release

release:
	ssh jenkins.admin.frm2 -p 29417 build -v -s -p GERRIT_PROJECT=$(shell git config --get remote.origin.url | rev | cut -d '/' -f -3 | rev) -p ARCH=all -p MODE=$(MODE) ReleasePipeline

.PHONY: release release-patch release-minor
