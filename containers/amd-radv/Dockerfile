FROM registry.opensuse.org/opensuse/leap:15.6

RUN zypper --non-interactive refresh && \
    zypper --non-interactive update && \
    zypper clean --all

COPY rocm_so.conf rocm_so.conf

RUN if ! zypper repos -E | grep devel_languages_perl; then \
    zypper --non-interactive addrepo https://download.opensuse.org/repositories/devel:/languages:/perl/15.6/devel:languages:perl.repo; \
    fi && \
    echo "[ROCm-7.14.0]" | tee --append /etc/zypp/repos.d/rocm.repo && \
    echo "name=ROCm7.14.0" | tee --append /etc/zypp/repos.d/rocm.repo && \
    echo "baseurl=https://repo.amd.com/rocm/packages-multi-arch/sles15/x86_64" | tee --append /etc/zypp/repos.d/rocm.repo && \
    echo "enabled=1" | tee --append /etc/zypp/repos.d/rocm.repo && \
    echo "gpgcheck=1" | tee --append /etc/zypp/repos.d/rocm.repo && \
    echo "gpgkey=https://repo.amd.com/rocm/packages-multi-arch/gpg/rocm.gpg" | tee --append /etc/zypp/repos.d/rocm.repo && \
    echo "priority=50" | tee --append /etc/zypp/repos.d/rocm.repo && \
    zypper --non-interactive --gpg-auto-import-keys refresh && \
    zypper --non-interactive install amdrocm-core-devel7.14-gfx90a && \
    zypper clean --all && \
    cat rocm_so.conf >> /etc/ld.so.conf.d/rocm.conf && \
    rm rocm_so.conf && \
	    ldconfig

ENV PATH="/opt/rocm/core-7.14/bin:${PATH}" \
    LD_LIBRARY_PATH="/opt/rocm/core-7.14/lib"

# Install the Vulkan loader and development headers, Mesa's AMD Vulkan driver
# (RADV, which uses ACO by default), and the tools needed to inspect and
# validate the generated SPIR-V on an allocated GPU node.
RUN zypper --non-interactive install \
      libvulkan1 \
      libvulkan_radeon \
      ninja \
      spirv-tools \
      vulkan-devel \
      vulkan-tools \
      vulkan-validationlayers && \
    zypper clean --all

RUN zypper --non-interactive install wget tar gzip gcc gcc-fortran file gcc-c++ make patch && zypper clean --all

# Install CMake
RUN zypper --non-interactive install cmake && zypper clean --all

# Install Python headers
RUN zypper --non-interactive install python3-devel && zypper clean --all

# Set environment variables for ROCm
ENV ROCM_PATH=/opt/rocm/core-7.14
ENV hip_DIR=/opt/rocm/core-7.14/lib/cmake/hip
ENV rocrand_DIR=/opt/rocm/core-7.14/lib/cmake/rocrand
ENV hiprand_DIR=/opt/rocm/core-7.14/lib/cmake/hiprand
ENV rocprim_DIR=/opt/rocm/core-7.14/lib/cmake/rocprim
ENV rocsparse_DIR=/opt/rocm/core-7.14/lib/cmake/rocsparse
ENV AMREX_AMD_ARCH=gfx90a
ENV CC=amdclang
ENV CXX=amdclang++

# add packages needed for full C++20 support
RUN zypper --non-interactive install gcc13 gcc13-c++ libstdc++6-devel-gcc13 && zypper clean --all

# add packages needed to download and build Quokka
RUN zypper --non-interactive install git gdb hdf5-devel python3-devel && zypper clean --all

# create user 'leap'
RUN useradd -ms /bin/bash leap
RUN usermod -a -G render,video leap
WORKDIR /home/leap
USER leap

# Set the default command to bash
CMD [ "/bin/bash" ]
