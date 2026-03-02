# Start Server
./memsrv_page 4444

# Pager (faulting pass + resident pass + flush)
./bench --header --mode=pager --ip=127.0.0.1 --port=4444 --region-mb=256 --pattern=seq --dirty=1 --passes=2

# RPC baseline (every pass does net read/write)
./bench --header --mode=rpc --ip=127.0.0.1 --port=4444 --region-mb=256 --pattern=seq --dirty=1 --passes=2

# Local baseline
./bench --header --mode=local --region-mb=256 --pattern=seq --dirty=1 --passes=2
