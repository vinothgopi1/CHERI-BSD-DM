
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

7. Run the benchmark files:
    ```
    
    ```
    Ensure all dependencies are met; you may need to install additional packages like `libevent` or others specific to your project.

8. Plot the graphs (Just dependent on the csv files so can be done locally):
    ```
    ./plot_benches.py
    ```
    Ensure all dependencies are met; you may need to install additional packages like `libevent` or others specific to your project.

### Troubleshooting
- If builds fail, check the CheriBSD documentation at [CTSRD-CHERI](https://github.com/CTSRD-CHERI/cheribsd).
- Ensure your CloudLab instance has sufficient resources (RAM, CPU) for compilation.