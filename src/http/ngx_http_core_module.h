
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HTTP_CORE_H_INCLUDED_
#define _NGX_HTTP_CORE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


#define NGX_HTTP_GZIP_PROXIED_OFF       0x0002
#define NGX_HTTP_GZIP_PROXIED_EXPIRED   0x0004
#define NGX_HTTP_GZIP_PROXIED_NO_CACHE  0x0008
#define NGX_HTTP_GZIP_PROXIED_NO_STORE  0x0010
#define NGX_HTTP_GZIP_PROXIED_PRIVATE   0x0020
#define NGX_HTTP_GZIP_PROXIED_NO_LM     0x0040
#define NGX_HTTP_GZIP_PROXIED_NO_ETAG   0x0080
#define NGX_HTTP_GZIP_PROXIED_AUTH      0x0100
#define NGX_HTTP_GZIP_PROXIED_ANY       0x0200


#define NGX_HTTP_AIO_OFF                0
#define NGX_HTTP_AIO_ON                 1
#define NGX_HTTP_AIO_SENDFILE           2


#define NGX_HTTP_SATISFY_ALL            0
#define NGX_HTTP_SATISFY_ANY            1


#define NGX_HTTP_LINGERING_OFF          0
#define NGX_HTTP_LINGERING_ON           1
#define NGX_HTTP_LINGERING_ALWAYS       2


#define NGX_HTTP_IMS_OFF                0
#define NGX_HTTP_IMS_EXACT              1
#define NGX_HTTP_IMS_BEFORE             2


#define NGX_HTTP_KEEPALIVE_DISABLE_NONE    0x0002
#define NGX_HTTP_KEEPALIVE_DISABLE_MSIE6   0x0004
#define NGX_HTTP_KEEPALIVE_DISABLE_SAFARI  0x0008


typedef struct ngx_http_location_tree_node_s  ngx_http_location_tree_node_t;
typedef struct ngx_http_core_loc_conf_s  ngx_http_core_loc_conf_t;

/**
 * listen 选项描述符
 */
typedef struct {
    /**
     * ip, port
     */
    union {
        struct sockaddr        sockaddr;
        struct sockaddr_in     sockaddr_in;
#if (NGX_HAVE_INET6)
        struct sockaddr_in6    sockaddr_in6;
#endif
#if (NGX_HAVE_UNIX_DOMAIN)
        struct sockaddr_un     sockaddr_un;
#endif
        u_char                 sockaddr_data[NGX_SOCKADDRLEN];
    } u;

    socklen_t                  socklen;

    unsigned                   set:1;
    unsigned                   default_server:1;
    unsigned                   bind:1;
    unsigned                   wildcard:1;
#if (NGX_HTTP_SSL)
    unsigned                   ssl:1;
#endif
#if (NGX_HAVE_INET6 && defined IPV6_V6ONLY)
    unsigned                   ipv6only:2;
#endif

    int                        backlog;
    int                        rcvbuf;
    int                        sndbuf;
#if (NGX_HAVE_SETFIB)
    int                        setfib;
#endif

#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)
    char                      *accept_filter;
#endif
#if (NGX_HAVE_DEFERRED_ACCEPT && defined TCP_DEFER_ACCEPT)
    ngx_uint_t                 deferred_accept;
#endif

    u_char                     addr[NGX_SOCKADDR_STRLEN + 1];   //!< 点十分进制的 human readable text of ip address:portNum, e.g. 1.1.1.1:80
} ngx_http_listen_opt_t;

//这个结构是定义了HTTP模块处理用户请求的11个阶段
typedef enum {
    //在接收到完整的HTTP头部后处理的HTTP阶段
    NGX_HTTP_POST_READ_PHASE = 0,

    //在还没有查询到URI匹配的location前，这时rewrite重写URL也作为一个独立的HTTP阶段
    NGX_HTTP_SERVER_REWRITE_PHASE,

    //根据URI寻找匹配的location
    NGX_HTTP_FIND_CONFIG_PHASE,

    //在查询到URI匹配的location之后的rewrite重写URL阶段
    NGX_HTTP_REWRITE_PHASE,

    //用于在rewrite重写URL后重新跳到NGX_HTTP_FIND_CONFIG_PHASE阶段
    NGX_HTTP_POST_REWRITE_PHASE,

    //处理NGX_HTTP_ACCESS_PHASE阶段前
    NGX_HTTP_PREACCESS_PHASE,

    //让HTTP模块判断是否允许这个请求访问NGINX服务器
    NGX_HTTP_ACCESS_PHASE,

    //当NGX_HTTP_ACCESS_PHASE阶段中HTTP模块的handler返回不允许访问的错误码的时候，
    //这个阶段负责构造拒绝服务的用户相应
    NGX_HTTP_POST_ACCESS_PHASE,

    //这个阶段完全是try_files配置项设立的。当HTTP请求访问静态文件资源的时候，try_files配置项
    //可以使这个请求顺序的访问多个静态文件资源。
    NGX_HTTP_TRY_FILES_PHASE,

    //用于处理HTTP请求内容的阶段。这个是大部分HTTP模块最喜欢介入的阶段。
    NGX_HTTP_CONTENT_PHASE,

    //处理完请求后记录日志的阶段。
    NGX_HTTP_LOG_PHASE
} ngx_http_phases;

typedef struct ngx_http_phase_handler_s  ngx_http_phase_handler_t;

// 一个HTTP处理阶段中的checker检查方法，仅可以由HTTP框架实现，以此控制HTTP请求的处理流程
typedef ngx_int_t (*ngx_http_phase_handler_pt)(ngx_http_request_t *r,
    ngx_http_phase_handler_t *ph);

struct ngx_http_phase_handler_s {
    /*
    在处理到某一个HTTP阶段时，HTTP框架将会在checker方法已实现的前提下首先调用checker方法来处理请求，而不会直接调用任何阶段汇总的handler方法。
    只有在checker方法中才会去调用handler方法。因此，事实上所有的checker方法都是由框架中的ngx_http_core_module模块实现的，且普通的HTTP模块
    无法重定义checker方法
    */
    ngx_http_phase_handler_pt  checker;

    /*
    除ngx_http_core_module模块以外的HTTP模块，只能通过定义handler方法才能介入某一个HTTP处理阶段以处理请求
    */
    ngx_http_handler_pt        handler;

    /*
    next的设计使得处理阶段不必按照顺序依次执行，既可以向后跳跃数个阶段继续执行，也可以跳跃到之前曾经执行过的某个阶段重新执行。
    通常，next表示下一个处理阶段中的第一个ngx_http_phase_handler_t处理方法
    */
    ngx_uint_t                 next;
};


typedef struct {
    /*
    handlers是由ngx_http_phase_handler_t构成的数组首地址，它表示一个请求可能经历的数个ngx_http_handler_pt处理方法
    */
    ngx_http_phase_handler_t  *handlers;

    /*
    表示NGX_HTTP_REWRITE_PHASE阶段第一个ngx_http_phase_handler_t处理方法在handlers数组中的序号，
    用于在执行HTTP请求的任何阶段中快速跳转到NGX_HTTP_SERVER_REWRITE_PHASE阶段处理请求
    */
    ngx_uint_t                 server_rewrite_index;

    /*
    表示NGX_HTTP_REWRITE_PHASE阶段第一个ngx_http_phase_handler_t处理方法在handlers数组中的序号，
    用于在执行HTTP请求的任何阶段中快速跳转到NGX_HTTP_SERVER_REWRITE_PHASE阶段处理请求
    */
    ngx_uint_t                 location_rewrite_index;
} ngx_http_phase_engine_t;


typedef struct {
    ngx_array_t                handlers;
} ngx_http_phase_t;

/**
 * ngx_http_core_module->ngx_http_core_create_main_conf 创建http模块 对应的main_conf描述符
 */
typedef struct {
    /**
     * 存储所有的ngx_http_core_srv_conf_t，元素的个数等于server块的个数
     */
    ngx_array_t                servers;         //!< 存放着 所有src_conf /* ngx_http_core_srv_conf_t */

    /** handler处理模块
     * 包含所有phase，以及注册的phase handler
     * 这些handler在处理http请求时，会被依次调用，通过ngx_http_phase_handler_t的next字段串联起来组成一个链表。 
     */
    ngx_http_phase_engine_t    phase_engine;

    ngx_hash_t                 headers_in_hash; //!< http request header各个头部对应的 handler回调, 以hash方式存储以加快索引速度

    /**
     * 被索引的nginx变量 ，比如通过rewrite模块的set指令设置的变量，会在这个hash 中分配空间
     * 而诸如$http_XXX和$cookie_XXX等内建变量不会在此分配空间。 
     */
    ngx_hash_t                 variables_hash;

    /**
     * ngx_http_variable_t类型的数组，所有被索引的nginx变量被存储在这个数组中。 
     * ngx_http_variable_t结构中有属性index，是该变量在这个数组的下标。 
     */
    ngx_array_t                variables;       /* ngx_http_variable_t */
    ngx_uint_t                 ncaptures;

    ngx_uint_t                 server_names_hash_max_size;      //!< server names的hash表的允许的最大bucket数量，默认值是512
    ngx_uint_t                 server_names_hash_bucket_size;   //!< server names的hash表中每个桶允许占用的最大空间，默认值是ngx_cacheline_size

    ngx_uint_t                 variables_hash_max_size;         //!< variables的hash表的允许的最大bucket数量，默认值是512
    ngx_uint_t                 variables_hash_bucket_size;      //!< variables的hash表中每个桶允许占用的最大空间，默认值是64

    /** 主要用于初始化variables_hash变量
     * 以hash方式存储所有的变量名，同时还保存 变量名和变量内容的kv对的数组，ngx_hash_t就是以这个数组进行初始化的。
     * 这个变量时临时的， 只在解析配置文件时有效，在初始化variables_hash后，会被置为NULL 
     */
    ngx_hash_keys_arrays_t    *variables_keys;

    /** 端口描述符 容器 - 监听的所有端口，ngx_http_port_t类型，其中包含socket地址信息
     * http main_conf ngx_http_core_main_conf_t->array(ngx_array_t)中 记录的所有 端口描述符(ngx_http_conf_port_t)
     * 而 ngx_http_conf_port_t->array (ngx_array_t) 中, 存放着所有引用当前端口描述的 地址描述符(ngx_http_conf_addr_t)
     */
    ngx_array_t               *ports;

    ngx_uint_t                 try_files;       /* unsigned  try_files:1 */

    /**
     * 所有的phase的数组，其中每个元素是该phase上注册的handler的数组
     */
    ngx_http_phase_t           phases[NGX_HTTP_LOG_PHASE + 1];
} ngx_http_core_main_conf_t;

/**
 * ngx_http_core_module->ngx_http_core_create_srv_conf 创建http模块 对应的srv_conf描述符
 */
typedef struct {
    /* array of the ngx_http_server_name_t, "server_name" directive */
    /**
     * 同一个 ngx_http_core_srv_conf_t, 可能会配有多个 基于域名的虚拟主机实例:
     *      e.g. server_names www.baidu.com
     *           server_names *.baidu.com
     * 
     * 所以,对于虚拟主机场景下, 利用ngx_array_t server_names存储 不同server_names 对应的 ngx_http_server_name_t 结构地址
     */
    ngx_array_t                 server_names;

    /* server ctx */
    ngx_http_conf_ctx_t        *ctx;        //!< 记录srv_conf 所属的ngx_http_conf_ctx_t

    ngx_str_t                   server_name;

    size_t                      connection_pool_size;
    size_t                      request_pool_size;
    size_t                      client_header_buffer_size;

    ngx_bufs_t                  large_client_header_buffers;

    ngx_msec_t                  client_header_timeout;

    ngx_flag_t                  ignore_invalid_headers;
    ngx_flag_t                  merge_slashes;
    ngx_flag_t                  underscores_in_headers;

    unsigned                    listen:1;   //!< 标示 该srv_conf 是否 配有listen (在server { 是否出现listen命令} )
#if (NGX_PCRE)
    unsigned                    captures:1;
#endif

    ngx_http_core_loc_conf_t  **named_locations;
} ngx_http_core_srv_conf_t;


/* list of structures to find core_srv_conf quickly at run time */


typedef struct {
    /* the default server configuration for this address:port */
    /**
     * default_server是在 ngx_http_add_address <- ngx_http_add_listen <- ngx_http_core_listen (listen命令的回调, 会输入server{}对应的 srv_conf(ngx_http_core_srv_conf_t) )
     */
    ngx_http_core_srv_conf_t  *default_server;

    ngx_http_virtual_names_t  *virtual_names;

#if (NGX_HTTP_SSL)
    ngx_uint_t                 ssl;   /* unsigned  ssl:1; */
#endif
} ngx_http_addr_conf_t;


typedef struct {
    in_addr_t                  addr;    //!< ip地址
    ngx_http_addr_conf_t       conf;    //!< ngx 地址描述符
} ngx_http_in_addr_t;


#if (NGX_HAVE_INET6)

typedef struct {
    struct in6_addr            addr6;
    ngx_http_addr_conf_t       conf;
} ngx_http_in6_addr_t;

#endif


typedef struct {
    /* ngx_http_in_addr_t or ngx_http_in6_addr_t */
    void                      *addrs;   //!< 指向 ngx_http_in_addr_t or ngx_http_in6_addr_t
    ngx_uint_t                 naddrs;  //!< 当前端口下 监听的 不同ip地址的个数
} ngx_http_port_t;

/** 端口描述符 <---> 地址描述符 <---> servername 匹配哈希表 && ngx_http_core_srv_conf_t
 *
 *      conf port descriptor                          conf ip descriptor                                                                                
 *		ngx_http_conf_port_t                          ngx_http_conf_addr_t                                                                                           
 *		    e.g. port=8080                             e.g. ip=127.0.0.1                                                                
 *
 *		+-----------------+                                                                                                               
 *		|                 |           +------------>--------------------------------+                                                     
 *		|                 |           |            |                                |                                                     
 *		|                 |           |            +--------------------------------+                                                     
 *		|                 |           |            |        ngx_hash_t hash         |                                                     
 *		+-----------------+           |            |exact hash for serverName       |                                                     
 *		|ngx_array_t addrs+-----------+            +--------------------------------+                    ngx_array_t servers              
 *		+-----------------+                        |                                |                                                     
 *		+-----------------+                        +--------------------------------+   +--->+-------------------------------------------+
 *		                                           |  ngx_hash_wildcard_t wc_head   |   |    |       addr of ngx_http_core_srv_conf_t    |
 *		                                           |headwildcard hash for ServerName|   |    |     server {servername www.baidu.com}     |
 *		                                           +--------------------------------+   |    +-------------------------------------------+
 *		                                           |                                |   |    |       addr of ngx_http_core_srv_conf_t    |
 *		                                           +--------------------------------+   |    |     server {servername www.sina.com}      |
 *		                                           |  ngx_hash_wildcard_t wc_tail   |   |    +-------------------------------------------+
 *		                                           |tailwildcard hash for ServerName|   |    |       addr of ngx_http_core_srv_conf_t    |
 *		                                           +--------------------------------+   |    |      server {servername www.zol.com}      |
 *		                                           |                                |   |    +-------------------------------------------+
 *		                                           |                                |   |                                                 
 *		                                           |                                |   |                                                 
 *		                                           +--------------------------------+   |                                                 
 *		                                           |      ngx_array_t servers       |---+                                                 
 *		                                           +--------------------------------+                                                     
 */
/**
 * 端口描述符
 */
typedef struct {
    // socket 地址家族
    ngx_int_t                  family;

    // 监听端口
    in_port_t                  port;

    // 监听的端口下对应着的所有ngx_http_conf_addr_t地址
    /**
     * 存放着 监听此端口的 所有地址描述符 (ngx_http_conf_addr_t)
     */
    ngx_array_t                addrs;     /* array of ngx_http_conf_addr_t */
} ngx_http_conf_port_t;

/** server address descriptor
 * 地址描述符
 */
typedef struct {
    // 监听套接字的各种属性
    ngx_http_listen_opt_t      opt;

    // 完全匹配server name的哈希表
    ngx_hash_t                 hash;        //!< 静态分配
    // 前置通配符 server name的哈希表
    ngx_hash_wildcard_t       *wc_head;     //!< 由ngx_hash_wildcard_init动态分配
    // 后置通配符 server name的哈希表
    ngx_hash_wildcard_t       *wc_tail;     //!< 由ngx_hash_wildcard_init动态分配

#if (NGX_PCRE)
    // 下面的regex数组中元素的个数
    ngx_uint_t                 nregex;
    /*
    regex指向静态数组，其数组成员就是ngx_http_server_name_t 结构体，表示正则表达式及其配置与server{} 虚拟主机
    */
    ngx_http_server_name_t    *regex;
#endif

    /* the default server configuration for this address:port */
    // 该监听端口下对应的默认 server{} 虚拟主机
    ngx_http_core_srv_conf_t  *default_server;  //!< srv_conf时创建的 ngx_http_core_srv_conf_t

    /** servers 动态数组中的成员将指向 srv_conf时创建的 ngx_http_core_srv_conf_t
     * 1. 虚拟主机场景: ip与port相同时, 虚拟主机场景(server_name配置不同)
     * 2. 此场景下,  一个 地址描述符 会指向多个 ngx_http_core_srv_conf_t, 故用此ngx_array_t容器存储起来
     */
    ngx_array_t                servers;  /* array of ngx_http_core_srv_conf_t */
} ngx_http_conf_addr_t;

/**
 * server_name 虚拟主机 描述符
 */
struct ngx_http_server_name_s {
#if (NGX_PCRE)
    ngx_http_regex_t          *regex;
#endif
    ngx_http_core_srv_conf_t  *server;      //!< value: virtual name server conf
    ngx_str_t                  name;        //!< key:   http Host
};


typedef struct {
    ngx_int_t                  status;
    ngx_int_t                  overwrite;
    ngx_http_complex_value_t   value;
    ngx_str_t                  args;
} ngx_http_err_page_t;


typedef struct {
    ngx_array_t               *lengths;
    ngx_array_t               *values;
    ngx_str_t                  name;

    unsigned                   code:10;
    unsigned                   test_dir:1;
} ngx_http_try_file_t;

/**
 * ngx_http_core_module->ngx_http_core_create_loc_conf 创建http模块 对应的loc_conf描述符
 */
struct ngx_http_core_loc_conf_s {
    // location 的名称，即nginx.conf 中location后的表达式
    ngx_str_t     name;          /* location name */

#if (NGX_PCRE)
    ngx_http_regex_t  *regex;   //!< 根据location输入的正则表达式, ngx_http_core_location -> ngx_http_core_regex_location用PCRE编译出的 正则结果描述符
#endif

    /**
     * 以下为location = 1.html {} location命令块标记
     */
    unsigned      noname:1;   /* "if () {}" block or limit_except */
    unsigned      lmt_excpt:1;
    unsigned      named:1;

    unsigned      exact_match:1;                        /* 完全匹配, e.g location = 1.html {} */
    unsigned      noregex:1;                            /* 优先最长前缀匹配, e.g location ^~ /static/ */

    unsigned      auto_redirect:1;
#if (NGX_HTTP_GZIP)
    unsigned      gzip_disable_msie6:2;
#if (NGX_HTTP_DEGRADATION)
    unsigned      gzip_disable_degradation:2;
#endif
#endif

    ngx_http_location_tree_node_t   *static_locations;  //!< 字符匹配树        由 ngx_http_init_static_location_trees 初始化
#if (NGX_PCRE)
    ngx_http_core_loc_conf_t       **regex_locations;   //!< 正则查询location  由 ngx_http_init_locations时初始化
#endif

    /* pointer to the modules' loc_conf */
    /**
     * 指向所属location块内ngx_http_conf_ctx_t结构体中的loc_conf指针数组，它保存着当前location块内所有HTTP模块crete_loc_conf方法产生的结构体指针
     */
    void        **loc_conf;

    uint32_t      limit_except;
    void        **limit_except_loc_conf;

    ngx_http_handler_pt  handler;

    /* location name length for inclusive location with inherited alias */
    size_t        alias;
    ngx_str_t     root;                         /* root, alias */
    ngx_str_t     post_action;

    ngx_array_t  *root_lengths;
    ngx_array_t  *root_values;

    ngx_array_t  *types;
    ngx_hash_t    types_hash;
    ngx_str_t     default_type;

    off_t         client_max_body_size;         /* client_max_body_size */
    off_t         directio;                     /* directio */
    off_t         directio_alignment;           /* directio_alignment */

    size_t        client_body_buffer_size;      /* client_body_buffer_size */
    size_t        send_lowat;                   /* send_lowat */
    size_t        postpone_output;              /* postpone_output */
    size_t        limit_rate;                   /* limit_rate */
    size_t        limit_rate_after;             /* limit_rate_after */
    size_t        sendfile_max_chunk;           /* sendfile_max_chunk */
    size_t        read_ahead;                   /* read_ahead */

    ngx_msec_t    client_body_timeout;          /* client_body_timeout */
    ngx_msec_t    send_timeout;                 /* send_timeout */
    ngx_msec_t    keepalive_timeout;            /* keepalive_timeout */
    ngx_msec_t    lingering_time;               /* lingering_time */
    ngx_msec_t    lingering_timeout;            /* lingering_timeout */
    ngx_msec_t    resolver_timeout;             /* resolver_timeout */

    ngx_resolver_t  *resolver;                  /* resolver */

    time_t        keepalive_header;             /* keepalive_timeout */

    ngx_uint_t    keepalive_requests;           /* keepalive_requests */
    ngx_uint_t    keepalive_disable;            /* keepalive_disable */
    ngx_uint_t    satisfy;                      /* satisfy */
    ngx_uint_t    lingering_close;              /* lingering_close */
    ngx_uint_t    if_modified_since;            /* if_modified_since */
    ngx_uint_t    max_ranges;                   /* max_ranges */
    ngx_uint_t    client_body_in_file_only;     /* client_body_in_file_only */

    ngx_flag_t    client_body_in_single_buffer;
                                                /* client_body_in_singe_buffer */
    ngx_flag_t    internal;                     /* internal */
    ngx_flag_t    sendfile;                     /* sendfile */
#if (NGX_HAVE_FILE_AIO)
    ngx_flag_t    aio;                          /* aio */
#endif
    ngx_flag_t    tcp_nopush;                   /* tcp_nopush */
    ngx_flag_t    tcp_nodelay;                  /* tcp_nodelay */
    ngx_flag_t    reset_timedout_connection;    /* reset_timedout_connection */
    ngx_flag_t    server_name_in_redirect;      /* server_name_in_redirect */
    ngx_flag_t    port_in_redirect;             /* port_in_redirect */
    ngx_flag_t    msie_padding;                 /* msie_padding */
    ngx_flag_t    msie_refresh;                 /* msie_refresh */
    ngx_flag_t    log_not_found;                /* log_not_found */
    ngx_flag_t    log_subrequest;               /* log_subrequest */
    ngx_flag_t    recursive_error_pages;        /* recursive_error_pages */
    ngx_flag_t    server_tokens;                /* server_tokens */
    ngx_flag_t    chunked_transfer_encoding     ; /* chunked_transfer_encoding */

#if (NGX_HTTP_GZIP)
    ngx_flag_t    gzip_vary;                    /* gzip_vary */

    ngx_uint_t    gzip_http_version;            /* gzip_http_version */
    ngx_uint_t    gzip_proxied;                 /* gzip_proxied */

#if (NGX_PCRE)
    ngx_array_t  *gzip_disable;                 /* gzip_disable */
#endif
#endif

    ngx_array_t  *error_pages;                  /* error_page */
    ngx_http_try_file_t    *try_files;          /* try_files */

    ngx_path_t   *client_body_temp_path;        /* client_body_temp_path */

    ngx_open_file_cache_t  *open_file_cache;
    time_t        open_file_cache_valid;
    ngx_uint_t    open_file_cache_min_uses;
    ngx_flag_t    open_file_cache_errors;
    ngx_flag_t    open_file_cache_events;

    ngx_log_t    *error_log;

    ngx_uint_t    types_hash_max_size;
    ngx_uint_t    types_hash_bucket_size;

    /**
     * 将同一个server块内多个表达location块的 ngx_http_core_loc_conf_t 结构体以及双向链表方式组合起来，
     * 该locations指针将指向ngx_http_location_queue_t 结构体
     */
    ngx_queue_t  *locations;

#if 0
    ngx_http_core_loc_conf_t  *prev_location;
#endif
};

/** nginx location 队列结构
 * nginx 构建static location tree(三叉树), 见 http://blog.chinaunix.net/uid-27767798-id-3759557.html
 *		                                                                                                                                                    
 *		   ngx_http_location_queue_t                                                                                                                       
 *		                                                                                                                                                   
 *		                                                                                                                                                   
 *		                                                                                                                                                   
 *		           ┌─────┐     ┌────┐       ┌────┐     ┌────┐      ┌────┐      ┌────┐    ┌────┐     ┌────┐     ┌────┐    ┌────┐   ┌────┐   ┌────┐    ┌────┐
 *		 RawData:  │ a1  │────▶│ aa │ ────▶ │aac │ ──▶ │aad │────▶ │ ab │────▶ │abc │ ──▶│abd │────▶│abda│ ───▶│abe │───▶│ ac ├──▶│ ad │──▶│ada │───▶│ ae │
 *		           └─────┘◀────└────┘ ◀──── └────┘ ◀───└────┘◀─────└────┘◀──── └────┘◀───└────┘◀────└────┘ ◀───└────┘◀───└────┘◀──└────┘◀──┴────┘◀───└────┘
 *		                                                                                                                                                   
 *		                                                                                                                                                   
 *		                          ┌─────┬────┐      ┌────┐        ┌────┬────┬────┐               ║                                                         
 *		 After Transformation:    │ a1  │ aa │ ─────│ ab │────────┤ ac │ ad │ ae │               ║                                                         
 *		                          └─────┴──╦─┘      └──╦─┘        └────┴─╦──┴────┘               ║   ngx_http_location_queue_t->list pointer               
 *		                                   ║           ║                 ║                       ║                                                         
 *		                                   ║           ║                 ║                       ║                                                         
 *		                                   ║           ║               ┌─▼──┐                    ║                                                         
 *		                                ┌──▼─┐      ┌──▼─┐             │ada │                    ▼     │                                                   
 *		                                │aac │      │abc │             └────┘                          │                                                   
 *		                                ├────┤      ├────┤     ┌────┐                                  │  ngx_http_location_queue_t->queue pointer         
 *		                                │aad │      │abd ╠═════▶abda│                                  │                                                   
 *		                                └────┘      ├────┤     └────┘                                  ▼                                                   
 *		                                            │abe │                                                                                                 
 *		                                            └────┘                                                                                                 
 */
typedef struct {
    // queue将作为ngx_queue_t 双向链表容器，从而将ngx_http_location_queue_t 结构体连接起来
    ngx_queue_t                      queue;
    // 如果location中的字符串可以精确匹配（包括正则），exact将指向对应的ngx_http_core_loc_conf_t结构体，否则值为null
    ngx_http_core_loc_conf_t        *exact;

    // 如果location中的字符串无法精确匹配（包括自定义的通配符），inclusive将指向对应的ngx_http_core_loc_conf_t 结构体，否则值为null
    ngx_http_core_loc_conf_t        *inclusive;

    // 指向location的名称
    ngx_str_t                       *name;
    u_char                          *file_name;
    ngx_uint_t                       line;
    ngx_queue_t                      list;  //!< 存储相同name前缀 的ngx_http_location_queue_t节点地址
} ngx_http_location_queue_t;

/** 数据面 find location config阶段的 三叉匹配树结构
 * 1. 三叉树 由上面的ngx_http_location_queue_t(ngx_http_core_location解析配置文件时生成) 在ngx_http_create_locations_list()编译而成
 * 2. ngx_http_core_find_config_phase为location数据面匹配函数
 */
struct ngx_http_location_tree_node_s {
    // 左子树
    ngx_http_location_tree_node_t   *left;  //!< 存放比当前节点 "小"的节点
    // 右子树
    ngx_http_location_tree_node_t   *right; //!< 存放比当前节点 "大"的节点
    // 无法完全匹配的location组成的树
    ngx_http_location_tree_node_t   *tree;  //!< 存放后缀节点, 前缀名为name

    /*
    如果location对应的URI匹配字符串属于能够完全匹配的类型，则exact指向其对应的ngx_http_core_loc_conf_t结构体，否则为NULL空指针
    */
    ngx_http_core_loc_conf_t        *exact;

    /*
    如果location对应的URI匹配字符串属于无法完全匹配的类型，则inclusive指向其对应的ngx_http_core_loc_conf_t 结构体，否则为NULL空指针
    */
    ngx_http_core_loc_conf_t        *inclusive;

    // 自动重定向标志
    u_char                           auto_redirect;

    // name字符串的实际长度
    u_char                           len;

    // name指向location对应的URI匹配表达式
    u_char                           name[1];
};


void ngx_http_core_run_phases(ngx_http_request_t *r);
ngx_int_t ngx_http_core_generic_phase(ngx_http_request_t *r,
    ngx_http_phase_handler_t *ph);
ngx_int_t ngx_http_core_rewrite_phase(ngx_http_request_t *r,
    ngx_http_phase_handler_t *ph);
ngx_int_t ngx_http_core_find_config_phase(ngx_http_request_t *r,
    ngx_http_phase_handler_t *ph);
ngx_int_t ngx_http_core_post_rewrite_phase(ngx_http_request_t *r,
    ngx_http_phase_handler_t *ph);
ngx_int_t ngx_http_core_access_phase(ngx_http_request_t *r,
    ngx_http_phase_handler_t *ph);
ngx_int_t ngx_http_core_post_access_phase(ngx_http_request_t *r,
    ngx_http_phase_handler_t *ph);
ngx_int_t ngx_http_core_try_files_phase(ngx_http_request_t *r,
    ngx_http_phase_handler_t *ph);
ngx_int_t ngx_http_core_content_phase(ngx_http_request_t *r,
    ngx_http_phase_handler_t *ph);


void *ngx_http_test_content_type(ngx_http_request_t *r, ngx_hash_t *types_hash);
ngx_int_t ngx_http_set_content_type(ngx_http_request_t *r);
void ngx_http_set_exten(ngx_http_request_t *r);
ngx_int_t ngx_http_send_response(ngx_http_request_t *r, ngx_uint_t status,
    ngx_str_t *ct, ngx_http_complex_value_t *cv);
u_char *ngx_http_map_uri_to_path(ngx_http_request_t *r, ngx_str_t *name,
    size_t *root_length, size_t reserved);
ngx_int_t ngx_http_auth_basic_user(ngx_http_request_t *r);
#if (NGX_HTTP_GZIP)
ngx_int_t ngx_http_gzip_ok(ngx_http_request_t *r);
#endif


ngx_int_t ngx_http_subrequest(ngx_http_request_t *r,
    ngx_str_t *uri, ngx_str_t *args, ngx_http_request_t **sr,
    ngx_http_post_subrequest_t *psr, ngx_uint_t flags);
ngx_int_t ngx_http_internal_redirect(ngx_http_request_t *r,
    ngx_str_t *uri, ngx_str_t *args);
ngx_int_t ngx_http_named_location(ngx_http_request_t *r, ngx_str_t *name);


ngx_http_cleanup_t *ngx_http_cleanup_add(ngx_http_request_t *r, size_t size);


typedef ngx_int_t (*ngx_http_output_header_filter_pt)(ngx_http_request_t *r);
typedef ngx_int_t (*ngx_http_output_body_filter_pt)
    (ngx_http_request_t *r, ngx_chain_t *chain);


ngx_int_t ngx_http_output_filter(ngx_http_request_t *r, ngx_chain_t *chain);
ngx_int_t ngx_http_write_filter(ngx_http_request_t *r, ngx_chain_t *chain);


extern ngx_module_t  ngx_http_core_module;

extern ngx_uint_t ngx_http_max_module;

extern ngx_str_t  ngx_http_core_get_method;


#define ngx_http_clear_content_length(r)                                      \
                                                                              \
    r->headers_out.content_length_n = -1;                                     \
    if (r->headers_out.content_length) {                                      \
        r->headers_out.content_length->hash = 0;                              \
        r->headers_out.content_length = NULL;                                 \
    }
                                                                              \
#define ngx_http_clear_accept_ranges(r)                                       \
                                                                              \
    r->allow_ranges = 0;                                                      \
    if (r->headers_out.accept_ranges) {                                       \
        r->headers_out.accept_ranges->hash = 0;                               \
        r->headers_out.accept_ranges = NULL;                                  \
    }

#define ngx_http_clear_last_modified(r)                                       \
                                                                              \
    r->headers_out.last_modified_time = -1;                                   \
    if (r->headers_out.last_modified) {                                       \
        r->headers_out.last_modified->hash = 0;                               \
        r->headers_out.last_modified = NULL;                                  \
    }

#define ngx_http_clear_location(r)                                            \
                                                                              \
    if (r->headers_out.location) {                                            \
        r->headers_out.location->hash = 0;                                    \
        r->headers_out.location = NULL;                                       \
    }


#endif /* _NGX_HTTP_CORE_H_INCLUDED_ */
