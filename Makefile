BASE_DIR = WTP-base/
OPT_DIR = WTP-opt/

.PHONY: clean all

all: base

base: 
	$(MAKE) -C $(BASE_DIR)

clean:
	$(MAKE) -C $(BASE_DIR) clean