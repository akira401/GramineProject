/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2014 Stony Brook University */

/*
 * This file contains the main function of the PAL loader, which loads and processes environment,
 * arguments and manifest.
 */

#include <asm/mman.h>
#include <linux/personality.h>

#include "api.h"
#include "asan.h"
#include "cpu.h"
#include "debug_map.h"
#include "elf/elf.h"
#include "etc_host_info.h"
#include "init.h"
#include "linux_utils.h"
#include "pal.h"
#include "pal_error.h"
#include "pal_internal.h"
#include "pal_linux.h"
#include "pal_linux_defs.h"
#include "pal_rtld.h"
#include "toml.h"
#include "toml_utils.h"
#include "topo_info.h"

char* g_pal_loader_path = NULL;
/* Currently content of this variable is only passed as an argument while spawning new processes
 * - this is to keep uniformity with other PALs. */
char* g_libpal_path = NULL;

struct pal_linux_state g_pal_linux_state;

const size_t g_page_size = PRESET_PAGESIZE;

/*
uint64_t g_pal_start_time = 0;
*/

static void read_info_from_stack(void* initial_rsp, int* out_argc, const char*** out_argv,
                                 const char*** out_envp, elf_addr_t* out_sysinfo_ehdr) {
    /* The stack layout on program entry is:
     *
     *            argc                  <-- `initial_rsp` points here
     *            argv[0]
     *            ...
     *            argv[argc - 1]
     *            argv[argc] = NULL
     *            envp[0]
     *            ...
     *            envp[n - 1] = NULL
     *            auxv[0]
     *            ...
     *            auxv[m - 1] = AT_NULL
     */
    const char** stack = (const char**)initial_rsp;
    int argc = (uintptr_t)stack[0];
    const char** argv = &stack[1];
    const char** envp = argv + argc + 1;
    assert(argv[argc] == NULL);

    const char** e = envp;
    for (; *e; e++) {}

    *out_sysinfo_ehdr = 0;
    for (elf_auxv_t* av = (elf_auxv_t*)(e + 1); av->a_type != AT_NULL; av++) {
        switch (av->a_type) {
            case AT_PAGESZ:
                if (av->a_un.a_val != g_page_size) {
                    INIT_FAIL("Unexpected AT_PAGESZ auxiliary vector");
                }
                break;
            case AT_SYSINFO_EHDR:
                *out_sysinfo_ehdr = av->a_un.a_val;
                break;
        }
    }

    *out_argc = argc;
    *out_argv = argv;
    *out_envp = envp;
}

noreturn static void print_usage_and_exit(const char* argv_0) {
    const char* self = argv_0 ?: "<this program>";
    log_always("USAGE:\n"
               "\tFirst process: %s <path to libpal.so> init <application> args...\n"
               "\tChildren:      %s <path to libpal.so> child <parent_stream_fd> "
               "<reserved_mem_ranges_fd> args...",
               self, self);
    log_always("This is an internal interface. Use gramine-direct wrapper to launch applications "
               "in Gramine.");
    _PalProcessExit(1);
}

static void get_host_etc_configs(void) {
    if (!g_pal_public_state.extra_runtime_domain_names_conf)
        return;

    if (parse_resolv_conf(&g_pal_public_state.dns_host) < 0) {
        INIT_FAIL("Unable to parse /etc/resolv.conf");
    }

    if (get_hosts_hostname(g_pal_public_state.dns_host.hostname,
                           sizeof(g_pal_public_state.dns_host.hostname)) < 0) {
        INIT_FAIL("Unable to get hostname");
    }
}

#ifdef ASAN
__attribute_no_stack_protector
__attribute_no_sanitize_address
static void setup_asan(void) {
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE;
    void* addr = (void*)DO_SYSCALL(mmap, (void*)ASAN_SHADOW_START, ASAN_SHADOW_LENGTH, prot, flags,
                                   /*fd=*/-1, /*offset=*/0);
    if (IS_PTR_ERR(addr) || addr != (void*)ASAN_SHADOW_START) {
        /* We are super early in the init sequence, TCB is not yet set, we probably should not call
         * any logging functions. */
        DO_SYSCALL(exit_group, 1);
        die_or_inf_loop();
    }
}
#endif

static int verify_hw_requirements(void) {
    unsigned int values[4];
    cpuid(FEATURE_FLAGS_LEAF, /*unused*/0, values);
    const char* missing = NULL;
    if (!(values[CPUID_WORD_ECX] & (1 << 25)))
        /* Technically we could change Gramine to require it only on PAL/Linux-SGX, but then we'd
         * need to ship two different builds of mbedtls (the one for SGX has to have software AES
         * implementation removed). */
        missing = "AES-NI";
    else if (!(values[CPUID_WORD_ECX] & (1 << 26)))
        missing = "XSAVE";
    if (missing) {
        log_error("Gramine with Linux backend requires %s CPU instruction(s). "
                  "Please upgrade your hardware.", missing);
        return -EINVAL;
    }
    return 0;
}

/* Gramine uses GCC's stack protector that looks for a canary at gs:[0x8], but this function starts
 * with no TCB in the GS register, so we disable stack protector here */
__attribute_no_stack_protector
__attribute_no_sanitize_address

noreturn void pal_linux_main(void* initial_rsp, void* fini_callback) {
    __UNUSED(fini_callback);  // TODO: We should call `fini_callback` at the end.
    int ret;

/* 
    ret = _PalSystemTimeQuery(&g_pal_start_time);
    if (ret < 0) {
        g_pal_start_time = 0;
    }
*/

#ifdef ASAN
    setup_asan();
#endif

    /* we don't yet have a TCB in the GS register, but GCC's stack protector will look for a canary
     * at gs:[0x8] in functions called below, so let's install a dummy TCB with a default canary */
    PAL_LINUX_TCB dummy_tcb_for_stack_protector = { 0 };
    dummy_tcb_for_stack_protector.common.self = &dummy_tcb_for_stack_protector.common;
    pal_tcb_set_stack_canary(&dummy_tcb_for_stack_protector.common,
                             STACK_PROTECTOR_CANARY_DEFAULT);
    ret = pal_set_tcb(&dummy_tcb_for_stack_protector.common);
    if (ret < 0) {
        DO_SYSCALL(exit_group, 1);
        die_or_inf_loop();
    }

/*
ret = _PalSystemTimeQuery(&g_pal_start_time);
if (ret < 0) {
    g_pal_start_time = 0;
}
*/


    /* relocate PAL */
    ret = setup_pal_binary();
    if (ret < 0) {
        INIT_FAIL("Relocation of the PAL binary failed: %d", ret);
    }

/* 追加 */
ret = PalSystemTimeQuery(&g_pal_public_state.migration_start_time);
if (ret < 0) {
    g_pal_public_state.migration_start_time = 0;
}
    uint64_t start_time;
    ret = _PalSystemTimeQuery(&start_time);
    if (ret < 0)
        INIT_FAIL("_PalSystemTimeQuery() failed: %s", pal_strerror(ret));

    call_init_array();

    g_pal_public_state.confidential_computing = false;

    g_pal_public_state.alloc_align = g_page_size;
    assert(IS_POWER_OF_2(g_pal_public_state.alloc_align));

    static_assert(THREAD_STACK_SIZE % PAGE_SIZE == 0, "");
    probe_stack(THREAD_STACK_SIZE / PAGE_SIZE);

    ret = init_memory_bookkeeping();
    if (ret < 0)
        INIT_FAIL("init_memory_bookkeeping failed: %s", pal_strerror(ret));

    ret = init_random();
    if (ret < 0)
        INIT_FAIL("init_random() failed: %s", pal_strerror(ret));

    int argc;
    const char** argv;
    const char** envp;
    elf_addr_t sysinfo_ehdr;
    read_info_from_stack(initial_rsp, &argc, &argv, &envp, &sysinfo_ehdr);

    if (argc < 4)
        print_usage_and_exit(argv[0]);

    set_pal_binary_name(argv[0]);

    ret = verify_hw_requirements();
    if (ret < 0)
        INIT_FAIL("verify_hw_requirements() failed");

    /* 起動モードの判定 */
    /* 最初の実行（init）*/
    bool first_process   = !strcmp(argv[2], "init");
    /* 子プロセスの実行（child）*/
    bool child_process   = !strcmp(argv[2], "child");
    /* マイグレーション先での実行（restore）*/
    bool restore_process = !strcmp(argv[2], "restore");

    g_pal_linux_state.host_environ = envp;

    PAL_HANDLE parent = NULL;
    char* manifest = NULL;
    uint64_t instance_id = 0;

    /* restoreモード判定変数 */
    g_pal_public_state.restore_nohandle = false;

    /* restoreモードの場合 */
    if (restore_process) {
        g_pal_public_state.restore_nohandle = true;
    }

    /* initの場合 */
    if (first_process) {
        ret = DO_SYSCALL(personality, 0xffffffffu);
        if (ret < 0){
            INIT_FAIL("retrieving personality failed: %s", unix_strerror(ret));
        }

        if (!(ret & ADDR_NO_RANDOMIZE)) {
            ret = DO_SYSCALL(personality, (unsigned int)ret | ADDR_NO_RANDOMIZE);
            if (ret < 0){
                INIT_FAIL("setting personality failed: %s", unix_strerror(ret));
            }
            ret = DO_SYSCALL(execve, "/proc/self/exe", argv, envp);
            INIT_FAIL("execve to disable ASLR failed: %s", unix_strerror(ret));
        }

#ifdef __x86_64__
        ret = DO_SYSCALL(arch_prctl, ARCH_REQ_XCOMP_PERM, AMX_TILEDATA);
        if (ret < 0 && ret != -EINVAL && ret != -EOPNOTSUPP && ret != -ENOSYS)
            INIT_FAIL("Requesting AMX permission failed: %s", unix_strerror(ret));
#endif
    }

    /* child */
    else if (child_process) {
        if (argc < 5){
            print_usage_and_exit(argv[0]);
        }

        int parent_stream_fd;
        int reserved_mem_ranges_fd;
        
        /* childの場合はfd番号（数字）が指定される */
        /* argv[4]にfd番号を指定 */
        reserved_mem_ranges_fd = atoi(argv[4]);

        ret = init_reserved_ranges(reserved_mem_ranges_fd);
        if (ret < 0) {
            INIT_FAIL("init_reserved_ranges failed: %s", pal_strerror(ret));
        }

        /* argv[3]にfd番号を指定 */
        parent_stream_fd = atoi(argv[3]);

        ret = DO_SYSCALL(fcntl, parent_stream_fd, F_SETFD, FD_CLOEXEC);
        if (ret < 0){
            INIT_FAIL("Failed to set `CLOEXEC`: %s", unix_strerror(ret));
        }

        /* ここで init_slab_mgr() を呼んでから init_child_process() へ */
        init_slab_mgr();
 
        init_child_process(parent_stream_fd, &parent, &manifest, &instance_id);
    }

    /* restore */
    else if (restore_process) {
        int parent_stream_fd;
        int reserved_mem_ranges_fd;

        if (argc < 6){
            INIT_FAIL("restore requires manifest path");
        }

       /* g_pal_public_state.checkpoint_file = argv[3]; */
       

        const char* manifest_path = argv[5];

        /* manifestファイルの読み込み */
        ret = read_text_file_to_cstr(manifest_path, &manifest);
        if (ret < 0)
            INIT_FAIL("Reading manifest failed: %s", unix_strerror(ret));
        
        /* restoreの場合はファイルパスが指定される */
        /* argv[4]にファイルパスを指定してオープン */
        reserved_mem_ranges_fd = DO_SYSCALL(open, argv[4], O_RDONLY, 0);
    
        if (reserved_mem_ranges_fd < 0) {
            INIT_FAIL("failed to open file.");
        }
        
        ret = init_reserved_ranges(reserved_mem_ranges_fd);
        if (ret < 0) {
            INIT_FAIL("init_reserved_ranges failed: %s", pal_strerror(ret));
        }

        /* argv[3]にファイルパスを指定してオープン */
        parent_stream_fd = DO_SYSCALL(open, argv[3], O_RDONLY, 0);
        
        if (parent_stream_fd < 0) {
            INIT_FAIL("failed to open file.");
        }
        
        ret = DO_SYSCALL(fcntl, parent_stream_fd, F_SETFD, FD_CLOEXEC);
        if (ret < 0){
            INIT_FAIL("Failed to set `CLOEXEC`: %s", unix_strerror(ret));
        }

       /* init_child_process(parent_stream_fd, &parent, &manifest, &instance_id); */   
      }


    /* init と restore はここで init_slab_mgr() を呼ぶ */
    if (first_process || restore_process) {
        init_slab_mgr();
    }

  /* init_slab_mgr()後にPalStreamOpenを実行 */
  if (restore_process) {
      char checkpoint_uri[256];
      snprintf(checkpoint_uri, sizeof(checkpoint_uri), "file:%s", argv[3]);

log_always("[pal_linux_main] about to call PalStreamOpen for restore checkpoint");
      ret = PalStreamOpen(checkpoint_uri, PAL_ACCESS_RDONLY, /*share_flags=*/0,
                          PAL_CREATE_NEVER, /*options=*/0, &parent);
log_always("[pal_linux_main] PalStreamOpen done, ret=%d, parent=%p", ret, parent);
      if (ret < 0)
          INIT_FAIL("failed to open checkpoint file: %s", pal_strerror(ret));

      /* 追加 */
      parent->file.seekable = 0;
      log_always("[pal_linux_main] after seekable=0: parent->hdr.type=%d, parent->file.fd=%d",
           parent->hdr.type, parent->file.fd);

      instance_id = 0;
  }

#ifdef DEBUG
    ret = debug_map_init_from_proc_maps();
    if (ret < 0)
        INIT_FAIL("failed to init debug maps: %s", unix_strerror(ret));
#endif

    if (first_process || restore_process) {
        ret = get_topology_info(&g_pal_public_state.topo_info);
        if (ret < 0)
            INIT_FAIL("get_topology_info() failed: %s", unix_strerror(ret));
    }

    g_pal_loader_path = get_main_exec_path();
    g_libpal_path = strdup(argv[1]);
    if (!g_pal_loader_path || !g_libpal_path)
        INIT_FAIL("Out of memory");

    PAL_HANDLE first_thread = calloc(1, HANDLE_SIZE(thread));
    if (!first_thread)
        INIT_FAIL("Out of memory");

    init_handle_hdr(first_thread, PAL_TYPE_THREAD);
    first_thread->thread.tid = DO_SYSCALL(gettid);

    void* alt_stack = calloc(1, ALT_STACK_SIZE);
    if (!alt_stack)
        INIT_FAIL("Out of memory");

    first_thread->thread.stack = alt_stack;

    PAL_LINUX_TCB* tcb = alt_stack + ALT_STACK_SIZE - sizeof(PAL_LINUX_TCB);
    pal_linux_tcb_init(tcb, first_thread, alt_stack, NULL, NULL);
    ret = pal_thread_init(tcb);
    if (ret < 0)
        INIT_FAIL("pal_thread_init() failed: %s", unix_strerror(ret));

    bool disable_vdso = false;
#ifdef __x86_64__
    uint32_t cpuid_7_0_values[4] = {0};
    cpuid(EXTENDED_FEATURE_FLAGS_LEAF, 0, cpuid_7_0_values);
    if (cpuid_7_0_values[CPUID_WORD_ECX] & (1u << 16))
        disable_vdso = true;
#endif

    if (sysinfo_ehdr && !disable_vdso) {
        ret = setup_vdso(sysinfo_ehdr);
        if (ret < 0)
            INIT_FAIL("Setup of VDSO failed: %s", pal_strerror(ret));
    }

    g_pal_linux_state.host_pid = DO_SYSCALL(getpid);

    if (first_process) {
        const char* application_path = argv[3];
        char* manifest_path = alloc_concat(application_path, -1, ".manifest", -1);
        if (!manifest_path)
            INIT_FAIL("Out of memory");

        ret = read_text_file_to_cstr(manifest_path, &manifest);
        if (ret < 0)
            INIT_FAIL("Reading manifest failed: %s", unix_strerror(ret));
    }

    assert(manifest);

    signal_setup(first_process,
                 disable_vdso ? 0 : g_vdso_start,
                 disable_vdso ? 0 : g_vdso_end);

    g_pal_common_state.raw_manifest_data = manifest;

    char errbuf[256];
    g_pal_public_state.manifest_root =
        toml_parse(manifest, errbuf, sizeof(errbuf));
    if (!g_pal_public_state.manifest_root)
        INIT_FAIL_MANIFEST(errbuf);

    ret = toml_bool_in(g_pal_public_state.manifest_root,
                       "sys.enable_extra_runtime_domain_names_conf",
                       false,
                       &g_pal_public_state.extra_runtime_domain_names_conf);
    if (ret < 0)
        INIT_FAIL("Cannot parse 'sys.enable_extra_runtime_domain_names_conf'");

    if (first_process)
        get_host_etc_configs();

    /* restore用オフセットを追加（argv + 6）*/
    pal_main(instance_id, parent, first_thread,
             first_process ? argv + 3 :
             child_process ? argv + 5 :
             argv + 6, 
             envp, NULL);
}


