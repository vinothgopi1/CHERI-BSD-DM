
## Accessing and Building CHERI on CloudLab

### Prerequisites
- A CloudLab account. Sign up at [CloudLab](https://www.cloudlab.us/) if you don't have one.
- Basic familiarity with Linux/Unix command line.

### Step 1: Access CloudLab and Instantiate a Profile
1. Log in to your CloudLab account.
2. Navigate to the "Experiments" section and select "Start Experiment".
3. Choose a profile that supports CheriBSD. Search for "CheriBSD" or "CHERI" profiles in the profile library (e.g., a profile like "CheriBSD on x86" or similar, depending on available options).
4. Select the appropriate hardware type (e.g., x86 servers) and instantiate the experiment. Wait for the instance to boot up.
5. Once ready, SSH into the instance using the provided credentials.

### Step 2: Build CHERI and the Project
1. On the CloudLab instance, update the system:
    ```
    sudo pkg update && sudo pkg upgrade
    ```
2. Install necessary dependencies for building CheriBSD/CHERI:
    ```
    sudo pkg install git llvm clang cmake ninja
    ```
3. Clone the CheriBSD repository:
    ```
    git clone https://github.com/vinothgopi1/CHERI-BSD-DM.git
    cd cheribsd
    ```
4. Configure and build CheriBSD with CHERI support (adjust for your architecture, e.g., for x86):
    ```
    ./cheribuild.py --include-dependencies run-riscv64-purecap
    ```
    This may take several hours. Follow any on-screen instructions for configuration.
5. Once built, clone your project repository (if not already present):
    ```
    git clone <your-repo-url>  # Replace with the actual repository URL for ECS251_Disaggregated_Memory
    cd ECS251_Disaggregated_Memory
    cd cheri_bench
    ```
6. Build the project:
    ```
    make
    ```
    Ensure all dependencies are met; you may need to install additional packages like `libevent` or others specific to your project.

7. Compile and run the benchmark files using the CHERI SDK. This will be done on the host. To send the binaries over to CHERI, you can use SSH and the scp command.
For compiling and running benchmark files on your host, use whatever tools are standard for your host environment.

    Ensure the CHERI SDK is available. By default the Makefile expects it at:

        ~/cheri/output/sdk

    Compile the benchmarks using the provided Makefile:

        make

    This will produce the following binaries:

        ptrchase
        mallocbench
        rpcbench

    Remove any previous result files:

        rm -f malloc_cheri.csv ptr_cheri.csv rpc_cheri.csv

    Run the malloc benchmark across multiple allocation sizes:

        for s in 16 32 64 128 256 512 1024 4096
        do
          echo "Running malloc size $s"
          ./mallocbench $s 100000 >> malloc_cheri.csv
        done

    Run the pointer-chasing benchmark across different working set sizes:

        for n in 10000 100000 1000000
        do
          echo "Running ptrchase N=$n"
          ./ptrchase $n 100 >> ptr_cheri.csv
        done

    For the RPC benchmark, start the server in a separate terminal:

        ./rpcbench server 9090

    Then run the client benchmark:

        for len in 16 64 512 4096 65536
        do
          case $len in
            16|64) iters=20 ;;
            512) iters=10 ;;
            4096) iters=5 ;;
            65536) iters=2 ;;
          esac

          for batch in 1 4 16
          do
            echo "Running len=$len iters=$iters batch=$batch"
            ./rpcbench client 127.0.0.1 9090 $len $iters $batch >> rpc_cheri.csv
          done
        done

    The benchmark results will be written to:

        malloc_cheri.csv
        ptr_cheri.csv
        rpc_cheri.csv

8. Plot the graphs (only depends on the generated CSV files, so this can be done locally):

    Run the plotting script:

    ```
    python3 plot_benches.py
    ```

    Ensure the required Python dependencies are installed. If not, install them with:

    ```
    pip install pandas matplotlib
    ```

    The script will read the benchmark CSV files and generate the corresponding benchmark graphs.

### Troubleshooting
- If builds fail, check the CheriBSD documentation at [CTSRD-CHERI](https://github.com/CTSRD-CHERI/cheribsd).
- Ensure your CloudLab instance has sufficient resources (RAM, CPU) for compilation.