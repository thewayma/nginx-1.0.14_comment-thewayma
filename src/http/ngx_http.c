
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


static char *ngx_http_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_init_phases(ngx_conf_t *cf,
    ngx_http_core_main_conf_t *cmcf);
static ngx_int_t ngx_http_init_headers_in_hash(ngx_conf_t *cf,
    ngx_http_core_main_conf_t *cmcf);
static ngx_int_t ngx_http_init_phase_handlers(ngx_conf_t *cf,
    ngx_http_core_main_conf_t *cmcf);

static ngx_int_t ngx_http_add_addresses(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *cscf, ngx_http_conf_port_t *port,
    ngx_http_listen_opt_t *lsopt);
static ngx_int_t ngx_http_add_address(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *cscf, ngx_http_conf_port_t *port,
    ngx_http_listen_opt_t *lsopt);
static ngx_int_t ngx_http_add_server(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *cscf, ngx_http_conf_addr_t *addr);

static char *ngx_http_merge_servers(ngx_conf_t *cf,
    ngx_http_core_main_conf_t *cmcf, ngx_http_module_t *module,
    ngx_uint_t ctx_index);
static char *ngx_http_merge_locations(ngx_conf_t *cf,
    ngx_queue_t *locations, void **loc_conf, ngx_http_module_t *module,
    ngx_uint_t ctx_index);
static ngx_int_t ngx_http_init_locations(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *cscf, ngx_http_core_loc_conf_t *pclcf);
static ngx_int_t ngx_http_init_static_location_trees(ngx_conf_t *cf,
    ngx_http_core_loc_conf_t *pclcf);
static ngx_int_t ngx_http_cmp_locations(const ngx_queue_t *one,
    const ngx_queue_t *two);
static ngx_int_t ngx_http_join_exact_locations(ngx_conf_t *cf,
    ngx_queue_t *locations);
static void ngx_http_create_locations_list(ngx_queue_t *locations,
    ngx_queue_t *q);
static ngx_http_location_tree_node_t *
    ngx_http_create_locations_tree(ngx_conf_t *cf, ngx_queue_t *locations,
    size_t prefix);

static ngx_int_t ngx_http_optimize_servers(ngx_conf_t *cf,
    ngx_http_core_main_conf_t *cmcf, ngx_array_t *ports);
static ngx_int_t ngx_http_server_names(ngx_conf_t *cf,
    ngx_http_core_main_conf_t *cmcf, ngx_http_conf_addr_t *addr);
static ngx_int_t ngx_http_cmp_conf_addrs(const void *one, const void *two);
static int ngx_libc_cdecl ngx_http_cmp_dns_wildcards(const void *one,
    const void *two);

static ngx_int_t ngx_http_init_listening(ngx_conf_t *cf,
    ngx_http_conf_port_t *port);
static ngx_listening_t *ngx_http_add_listening(ngx_conf_t *cf,
    ngx_http_conf_addr_t *addr);
static ngx_int_t ngx_http_add_addrs(ngx_conf_t *cf, ngx_http_port_t *hport,
    ngx_http_conf_addr_t *addr);
#if (NGX_HAVE_INET6)
static ngx_int_t ngx_http_add_addrs6(ngx_conf_t *cf, ngx_http_port_t *hport,
    ngx_http_conf_addr_t *addr);
#endif

ngx_uint_t   ngx_http_max_module;   //!< ngx_modules中 属于NGX_HTTP_MODULE模块 的个数 


ngx_int_t  (*ngx_http_top_header_filter) (ngx_http_request_t *r);
ngx_int_t  (*ngx_http_top_body_filter) (ngx_http_request_t *r, ngx_chain_t *ch);


ngx_str_t  ngx_http_html_default_types[] = {
    ngx_string("text/html"),
    ngx_null_string
};


static ngx_command_t  ngx_http_commands[] = {

    { ngx_string("http"),
      NGX_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_http_block,
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_core_module_t  ngx_http_module_ctx = {
    ngx_string("http"),
    NULL,
    NULL
};


ngx_module_t  ngx_http_module = {
    NGX_MODULE_V1,
    &ngx_http_module_ctx,                  /* module context */
    ngx_http_commands,                     /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

/** 关于虚拟主机场景, 包括以下3种
 * 虚拟主机定义: 一个nginx实例, 同时为多个域名同时服务
 *
 * 情况1. 基于域名
 *      server {
 *          listen 192.168.1.1:80;
 *          server_name www.test1.com;
 *      }
 *
 *      server {
 *          listen 192.168.1.1:80;
 *          server_name www.test2.com;
 *      }
 *
 *      server {
 *          listen 192.168.1.1.:80;
 *          server_name *.test3.com
 *      }
 *      
 *      server {
 *          listen 192.168.1.1.:80;
 *          server_name www.test4*
 *      }
 *
 *   1). 此时, www.test1.com 与 www.test2.com 对应的 ngx_http_core_srv_conf_t 分别插入到 192.168.1.1:80 对应的 server_name完全匹配哈希表中
 *                *.test3.com 与 www.test3*      对应的 ngx_http_core_srv_conf_t 分别插入到 192.168.1.1:80 对应的 server_name前后缀匹配哈希表中
 *   2). 数据面, ngx_http_find_virtual_server中 会根据请求的Host 在server_name 完全/前后缀匹配哈希表 进行查找对应的 ngx_http_core_srv_conf_t
 *
 *
 *
 * 情况2. 基于ip 
 *      server {
 *          listen 192.168.1.1:80;
 *          server_name www.test1.com;
 *      }
 *
 *      server {
 *          listen 192.168.1.2:80;
 *          server_name www.test2.com;
 *      }
 *
 *   1). 此时, 配置端口描述符(ngx_http_conf_port_t)->配置地址描述符(ngx_http_conf_addr_t)->server_name匹配哈希表(hash/wc_head/wc_tail)为空
 *            因为ngx_http_conf_addr_t下存在多个注册的ngx_http_core_srv_conf_t时, ngx_http_optimize_servers才会调用 ngx_http_server_names 初始化server_name哈希表
 *            i.e. ngx_http_conf_addr_t下存在多个注册的ngx_http_core_srv_conf_t  只有像 情况1中的配置(listen同一ip:port, 配置多个server{server_name})
 *   2). 数据面, 每个accept收上来的请求, getsockname都可以获得 本地监听的ip:port, 然后根据default_server 找到所属的 ngx_http_core_srv_conf_t
 *
 *
 *
 * 情况3. 基于端口
 *      server {
 *          listen 192.168.1.1:80;
 *          server_name www.test1.com;
 *      }
 *
 *      server {
 *          listen 192.168.1.1:81;
 *          server_name www.test2.com;
 *      }
 *   1). 此时, 配置端口描述符(ngx_http_conf_port_t)->配置地址描述符(ngx_http_conf_addr_t)->server_name匹配哈希表(hash/wc_head/wc_tail)为空
 *   2). 数据面, 每个accept收上来的请求, getsockname都可以获得 本地监听的ip:port, 然后根据default_server 找到所属的 ngx_http_core_srv_conf_t
 *
 *
 * 参考:
 *      Nginx实现多虚拟主机配置 https://blog.csdn.net/daybreak1209/article/details/51546332
 */

/** ngx_http_block http命令块的解析回调, NGX_MAIN_CONF,要创建main_conf 
 * 碰到http指令, 执行以下重要内容
 * 1. 分配内存: 
 *      为http ctx分配 ngx_http_conf_ctx_t, 并 依次调用每个http模块的 create_main_conf, create_srv_conf, create_srv_conf
 * 
 * 2. 解析配置脚本:
 *      解析配置脚本中的 http{} 块内的指令 递归触发server和location的解析: ---> server{}分配内存与配置解析 ---> location{}分配内存与配置解析
 *
 *      配置样例:
 *      http {
 *          server {                        --- ngx_http_core_srv_conf_t A1 
 *              listen 1.1.1.1:80           --- 属于同一个 端口描述符(ngx_http_conf_port_t) 与 地址描述符(ngx_http_conf_addr_t)
 *              server_name www.abc.com
 *
 *              location = /1.html {
 *
 *              }
 *
 *          }
 *
 *          server {                        --- ngx_http_core_srv_conf_t A2
 *              listen 1.1.1.1:80           --- 属于同一个 端口描述符(ngx_http_conf_port_t) 与 地址描述符(ngx_http_conf_addr_t)
 *              server_name www.efg.com    
 *
 *              location = /1.html {
 *
 *              }
 *          }
 *      }
 *
 *      其中重要的命令如下:
 *      2.1. event{} 命令块解析函数     ngx_events_block
 *
 *      2.2. http {} 命令块解析函数     ngx_http_block
 *
 *      2.3. server{} 命令块解析函数    ngx_http_core_server, 将新创建的ngx_http_core_srv_conf_t *cscf 插入 父(main_conf)ngx_http_core_main_conf_t   *cmcf->servers(ngx_array_t)上
 *
 *      2.4. listen 80, 命令解析函数    ngx_http_core_listen -> ngx_http_add_listen -> ngx_http_add_addresses -> ngx_http_add_server,   
 *                                              1). 向 ngx_http_block创建的 ngx_http_core_main_conf_t->ports (ngx_array_t) 中插入
 *                                                                  ngx_http_listen_opt_t lsopt对应的 端口描述符(ngx_http_conf_port_t) 与 地址描述符(ngx_http_conf_addr_t)
 *                                              2). 将srv_conf时创建的 ngx_http_core_srv_conf_t *cscf 插入到
 *                                                                  ngx_http_conf_addr_t->servers(ngx_array_t, 地址描述符存储cscf的容器)
 *
 *      2.5. server_name, 命令解析函数  ngx_http_core_server_name, ngx_http_core_srv_conf_t->server_names(ngx_array_t) 存储 不同server_name 对应的 ngx_http_server_name_t 结构地址
 *
 *      2.6. location = /abc/1.html     ngx_http_core_location -> ngx_http_add_location, 将当前ngx_http_core_loc_conf_t* clcf 链入到
 *                                                                                          父parent_ctx(srv_conf) 对应的ngx_http_core_loc_conf_s->ngx_queue_t双向链表上
 *
 * 3. 构造 static location tree 三叉匹配树 ---------------------------------- 用于location 匹配 --------------------------------------------------------------------
 *      3.1. 用途: 精确匹配(location = /abc), 前缀开始匹配(location ~* /abc) 
 *
 *      3.2. ngx_http_init_locations 去重, 排序字符 
 *              将用户location输入的 精确匹配和前缀匹配字符串 进行 排序与去重 工作
 *              具体见 ngx_http_init_locations -> ngx_queue_sort(locations, ngx_http_cmp_locations)
 *
 *              ngx_queue_sort排序完成后, 各类型的location_queue_t按照以下顺序排列
 *                  1). 完全匹配, 前缀匹配(优先前缀匹配, 普通前缀匹配) << 正则匹配(按照配置文件的先后顺序, 从左至右排列) << 其他(noname << named) 
 *                      (a). /abc    vs.   =/abc        ===> 长度一样时(strcmp==0), 完全匹配必须放在前面    
 *                      (b). /abc    vs.   =/abcd       ===> 长度不一样时(strcmp<0), 只按照strcmp输出, 前缀匹配 在 完全匹配 前
 *                  2). 同种类型内部, 按照strcmp 按照字母升序排列
 *
 *              最后, location链表 从第一个正则匹配节点开始一分为二
 *                  1). 前半段(完全和前缀匹配)
 *                      前半段节点 依然保留在 pclcf->locations容器中
 *                  2). 后半段(正则匹配)
 *                      正则链表q,  lq = (ngx_http_location_queue_t *) q;
 *                      将每个(ngx_http_core_loc_conf_t *)(lq->exact) 新建location的地址 存放在 父层容器pclcf->regex_locations二维数据组里
 *
 *      3.3. ngx_http_init_static_location_trees 为 完全和前缀(优先, 普通前缀)匹配  生成static location tree 三叉树
 *              (1). 将排序后(按照name从小到大顺序, 里面可能有相同前缀)的ngx_http_location_queue_t, 生成相应的location list队列
 *                   具体见 ngx_http_create_locations_list(locations//链表头部, ngx_queue_head(locations)//第一个有效元素);
 *
 *              (2). 根据above location list, 递归生成 nginx 三叉树( 用于 http uri与location配置匹配 )
 *                   具体见 pclcf->static_locations = ngx_http_create_locations_tree(cf, locations, 0);
 *      
 *      3.4. location匹配过程(数据面查找逻辑)
 *              ngx_http_core_find_config_phase -> ngx_http_core_find_location -> ngx_http_core_find_static_location(完全精确匹配, 优先前缀匹配, 普通前缀匹配)
 *                                                                             -> ngx_http_regex_exec(普通前缀匹配, 正则匹配)
 *
 *      3.5. 参考
 *          1). [nginx] location定位, https://blog.csdn.net/mqfcu8/article/details/55001139
 *          2). nginx构建static location tree的过程, http://blog.chinaunix.net/uid-27767798-id-3759557.html
 *
 * 4. ngx_http_init_headers_in_hash 根据ngx_http_headers_in 初始化http request header 回调, 回调调用时机: ngx_http_process_request_headers
 *
 * 5. Handler模块:
 *      1). 初始化Handler的 11个phase: ngx_http_init_phases, ngx_http_init_phase_handlers
 *      2). 数据面(框架运行): ngx_http_core_run_phases
 *
 * 6. listen监听信息:
 *      ngx_http_optimize_servers 初始化socket相关信息 (listen, bind) 
 *
 *      //!< --------------------------------------- 用于server_name 与 Http Host字段的匹配 ----------------------------------------------------------------------
 *      对于虚拟主机场景之一 基于域名(ip:port相同, 但src_conf创建不同server_name 对应的不同ngx_http_core_srv_conf_t)
 *      6.1. 创建server_name哈希表
 *              ngx_http_optimize_servers -> ngx_http_server_names 为不同server_name创建相应的哈希表(普通, 前置wildcard, 后置wildcard)
 *      
 *      6.2. 虚拟主机匹配(数据面查找逻辑)
 *              在 Nginx 处理请求过程中，在函数 ngx_http_find_virtual_server 中，根据请求包头 的 “Host” 字段内容，
 *              使用 ngx_hash_find_combined 函数对虚拟主机名哈希表中进行 查找匹配，寻找合适的虚拟主机
 *      
 *      6.3. ngx_http_init_listening, 初始化listen
 */
static char *ngx_http_block(ngx_conf_t *cf/* cf->ctx 为ngx_init_cycle创建的全局 cycle->conf_ctx = ngx_pcalloc(pool, ngx_max_module * sizeof(void *))*/,
        ngx_command_t *cmd, void *conf/* &(((void **) cycle->conf_ctx)[ngx_http_module->index])*/)  //!< 解析 http {} 块  里的配置指令
{
    char                        *rv;
    ngx_uint_t                   mi, m, s;
    ngx_conf_t                   pcf;
    ngx_http_module_t           *module;
    ngx_http_conf_ctx_t         *ctx;
    ngx_http_core_loc_conf_t    *clcf;
    ngx_http_core_srv_conf_t   **cscfp;
    ngx_http_core_main_conf_t   *cmcf;

    /* the main http context */
    ctx = ngx_pcalloc(cf->pool, sizeof(ngx_http_conf_ctx_t));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

	//最核心的地方，可以看到修改了传递进来的conf
    *(ngx_http_conf_ctx_t **) conf = ctx;


    /* count the number of the http modules and set up their indices */
    //初始化所有的http module的ctx_index
    ngx_http_max_module = 0;
    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_HTTP_MODULE) {
            continue;
        }
        
        //每个模块都有自己对应的索引值
        ngx_modules[m]->ctx_index = ngx_http_max_module++;
    }

    /**
     * 配置文件中 一遇到http {}
     * 就会为 main, srv, loc 分配ngx_http_max_module 个 void *
     */

    //下面是创建http module的对应的main,srv,loc config
    /* the http main_conf context, it is the same in the all http contexts */
    
    //开始初始化，可以看到默认会分配max个config 
    //创建HTTP对应的conf，因为每个级别(main/srv/loc)都会包含模块的conf
    ctx->main_conf = ngx_pcalloc(cf->pool,
                                 sizeof(void *) * ngx_http_max_module);
    if (ctx->main_conf == NULL) {
        return NGX_CONF_ERROR;
    }


    /** 配置文件中 一遇到http {}  就会为 srv 分配ngx_http_max_module 个 void *
     * the http null srv_conf context, it is used to merge
     * the server{}s' srv_conf's
     */
    ctx->srv_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module);
    if (ctx->srv_conf == NULL) {
        return NGX_CONF_ERROR;
    }


    /** 配置文件中 一遇到http {}  就会为 loc 分配ngx_http_max_module 个 void *
     * the http null loc_conf context, it is used to merge
     * the server{}s' loc_conf's
     */
    ctx->loc_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module);
    if (ctx->loc_conf == NULL) {
        return NGX_CONF_ERROR;
    }


    /*
     * create the main_conf's, the null srv_conf's, and the null loc_conf's
     * of the all http modules
     */

    //调用对应的create_xxx_conf回调函数
    //开始遍历
    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_HTTP_MODULE) {
            continue;
        }
        
        //得到对应的module上下文
        module = ngx_modules[m]->ctx;
        //得到对应的索引 
        mi = ngx_modules[m]->ctx_index;
        
        //如果有对应的回调，则调用回调函数，然后将返回的模块config设置到ctx的对应的conf列表中
        if (module->create_main_conf) {
            ctx->main_conf[mi] = module->create_main_conf(cf);
            if (ctx->main_conf[mi] == NULL) {
                return NGX_CONF_ERROR;
            }
        }

        if (module->create_srv_conf) {
            ctx->srv_conf[mi] = module->create_srv_conf(cf);
            if (ctx->srv_conf[mi] == NULL) {
                return NGX_CONF_ERROR;
            }
        }

        if (module->create_loc_conf) {
            ctx->loc_conf[mi] = module->create_loc_conf(cf);
            if (ctx->loc_conf[mi] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
    }

	//保存当前使用的cf，因为我们只是在解析HTTP时需要改变当前的cf
    pcf = *cf;
	//保存当前模块的上下文
    cf->ctx = ctx;  //!< 解析http模块的配置上下文 ngx_http_conf_ctx_t

    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_HTTP_MODULE) {
            continue;
        }

        module = ngx_modules[m]->ctx;
        
        //如果存在preconfiguratio则调用初始化,真正初始化模块之前需要调用preconfiguration来进行一些操作。
        if (module->preconfiguration) {
            if (module->preconfiguration(cf) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }
    }

    /* ------------------------------------ parse inside the http{} block ----------------------------------------- */

	//设置模块类型和命令类型
    cf->module_type = NGX_HTTP_MODULE;
    cf->cmd_type = NGX_HTTP_MAIN_CONF;

    //继续parse config,这里注意传递进去的文件名是空
    /** !!! 迭代过程 !!!
     * 解析http模块的配置上下文 ngx_http_conf_ctx_t
     *
     * 注意上面的: cf->ctx = ctx;
     *
     *
     * server{} 解析命令为ngx_http_core_server
     */
    rv = ngx_conf_parse(cf, NULL);

    if (rv != NGX_CONF_OK) {
        goto failed;
    }

    /**
     * init http{} main_conf's, merge the server{}s' srv_conf's and its location{}s' loc_conf's
     */
    cmcf = ctx->main_conf[ngx_http_core_module.ctx_index];  //!< main 为ngx_http_core_module(自身)分配的 ngx_http_core_main_conf_t
    cscfp = cmcf->servers.elts;

    //当http block完全parse完毕之后，就需要merge(main和srv或者srv和loc)相关的config了。不过在每次merge之前都会首先初始化main conf。
    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_HTTP_MODULE) {
            continue;
        }

        //首先取得模块以及对应索引
        module = ngx_modules[m]->ctx;
        mi = ngx_modules[m]->ctx_index;

        /* init http{} main_conf's */

        //如果有init_main_conf,则首先初始化main conf
        if (module->init_main_conf) {
            rv = module->init_main_conf(cf, ctx->main_conf[mi]);
            if (rv != NGX_CONF_OK) {
                goto failed;
            }
        }

        //然后开始merge srv和loc的配置
        rv = ngx_http_merge_servers(cf, cmcf, module, mi);
        if (rv != NGX_CONF_OK) {
            goto failed;
        }
    }

    /* create location trees */
    //当merge完毕之后，然后就是初始化location tree，创建handler phase，调用postconfiguration，以及变量的初始化
    for (s = 0; s < cmcf->servers.nelts; s++) {
        /** 当前for逻辑
         * 依次迭代每个server{}配置块, 依次初始化每个server{}块对应的 static location tree(三叉location匹配树), 即 srv_conf[0]  loc_conf[0]->static_locations
         */

        /**
         * location{} 解析命令, ngx_http_core_location -> ngx_http_add_location, 将当前loc_conf 链入到 parent_ctx(srv_conf) 对应的ngx_http_core_loc_conf_t->(ngx_queue_t *)locations 双向链表上
         * clcf->(ngx_queue_t *)locations双向链表, 即main_conf下挂各个server{}块 所属的ngx_queue_t双向链表, 此链表 存储 server{location{}} 里所有location 对应的 ngx_http_core_loc_conf_t地址
         */
        clcf = cscfp[s]->ctx->loc_conf[ngx_http_core_module.ctx_index]; //!< main_conf下挂的各个src_conf所属的 ngx_http_conf_ctx_t->loc_conf[0]
        //!<   |----------------------|
        //!<   |各个server块的ngx_http_core_loc_conf_t |
        //!<   |----------------------|

        /**
         * 主要将 精确匹配(location = /abc), 前缀匹配(location ^~ /abc | location /abc) 进行排序
         * 具体见 ngx_queue_sort(locations, ngx_http_cmp_locations)
         *
         * 1. ngx_queue_sort排序完成后, 各类型的location_queue_t按照以下顺序排列
         *      1.1. 完全匹配 << 前缀匹配(优先前缀匹配, 普通前缀匹配) << 正则匹配(按照配置文件的先后顺序, 从左至右排列) << 其他(noname << named) 
         *      1.2. 同种类型内部, 按照strcmp 按照字母升序排列
         *
         * 2. 最后, location链表 从第一个正则匹配节点开始一分为二
         *      2.1. 前半段(完全和前置匹配)
         *          前半段节点 依然保存在 pclcf->locations容器中
         *      2.2. 后半段(正则匹配)
         *          正则链表q,  lq = (ngx_http_location_queue_t *) q;
         *          将每个(ngx_http_core_loc_conf_t *)(lq->exact) 新建location的地址 存放在 父层容器pclcf->regex_locations二维数据组里
         */
        if (ngx_http_init_locations(cf, cscfp[s]/*各个srv_conf对应的 ngx_http_core_srv_conf_t地址*/, clcf/*srv_conf->loc_conf[0]*/) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        /** 构建前缀匹配(完全匹配, 优先前缀, 普通前缀)三叉匹配树
         * 1. 将排序后(按照name从小到大顺序, 里面可能有相同前缀)的ngx_http_location_queue_t, 生成相应的location list队列
         *      具体见 ngx_http_create_locations_list(locations//链表头部, ngx_queue_head(locations)//第一个有效元素);
         *
         *      nginx 构建static location tree(三叉树), 见 http://blog.chinaunix.net/uid-27767798-id-3759557.html
		 *					                                                                                                                                                    
		 *		    ngx_http_location_queue_t                                                                                                                       
		 *		                                                                                                                                                    
		 *		                                                                                                                                                    
		 *		                                                                                                                                                    
		 *		            ┌─────┐     ┌────┐       ┌────┐     ┌────┐      ┌────┐      ┌────┐    ┌────┐     ┌────┐     ┌────┐    ┌────┐   ┌────┐   ┌────┐    ┌────┐
		 *		  RawData:  │ a1  │────▶│ aa │ ────▶ │aac │ ──▶ │aad │────▶ │ ab │────▶ │abc │ ──▶│abd │────▶│abda│ ───▶│abe │───▶│ ac ├──▶│ ad │──▶│ada │───▶│ ae │
		 *		            └─────┘◀────└────┘ ◀──── └────┘ ◀───└────┘◀─────└────┘◀──── └────┘◀───└────┘◀────└────┘ ◀───└────┘◀───└────┘◀──└────┘◀──┴────┘◀───└────┘
		 *		                                                                                                                                                    
		 *		                                                                                                                                                    
		 *		                           ┌─────┬────┐      ┌────┐        ┌────┬────┬────┐               ║                                                         
		 *		  After Transformation:    │ a1  │ aa │ ─────│ ab │────────┤ ac │ ad │ ae │               ║                                                         
		 *		                           └─────┴──╦─┘      └──╦─┘        └────┴─╦──┴────┘               ║   ngx_http_location_queue_t->list pointer               
		 *		                                    ║           ║                 ║                       ║                                                         
		 *		                                    ║           ║                 ║                       ║                                                         
		 *		                                    ║           ║               ┌─▼──┐                    ║                                                         
		 *		                                 ┌──▼─┐      ┌──▼─┐             │ada │                    ▼     │                                                   
		 *		                                 │aac │      │abc │             └────┘                          │                                                   
		 *		                                 ├────┤      ├────┤     ┌────┐                                  │  ngx_http_location_queue_t->queue pointer         
		 *		                                 │aad │      │abd ╠═════▶abda│                                  │                                                   
		 *		                                 └────┘      ├────┤     └────┘                                  ▼                                                   
		 *		                                             │abe │                                                                                                 
		 *		                                             └────┘                                                                                                 
         *
         * 2. 根据above location list, 递归生成 nginx 三叉树( 用于 http uri与location配置匹配 )
         *      具体见 pclcf->static_locations = ngx_http_create_locations_tree(cf, locations, 0);
         */
        if (ngx_http_init_static_location_trees(cf, clcf) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }   //!< for {}, 依次迭代每个server{}配置块, 依次初始化每个server{}块对应的 static location tree(三叉location匹配树), 即 srv_conf[0]  loc_conf[0]->static_locations

    //初始化handler phase array数组 
    if (ngx_http_init_phases(cf, cmcf) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    //!< ngx_http_init_headers_in_hash 根据ngx_http_headers_in 初始化http request header 回调, 回调调用时机: ngx_http_process_request_headers
    if (ngx_http_init_headers_in_hash(cf, cmcf) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    //遍历模块，然后调用对应的postconfiguration
    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_HTTP_MODULE) {
            continue;
        }

        module = ngx_modules[m]->ctx;
        
        //调用回调
        if (module->postconfiguration) {
            if (module->postconfiguration(cf) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }
    }
    
    //开始初始化变量 - before 这个函数前, ngx_http_core_preconfiguration->ngx_http_variables_add_core_vars 根据ngx_http_core_variables[] 初始化 内置动态变量
    if (ngx_http_variables_init_vars(cf) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    /*
     * http{}'s cf->ctx was needed while the configuration merging
     * and in postconfiguration process
     */

	//恢复cf
    *cf = pcf;

    /**
     * 1. 初始化: http 11 phase handler处理框架
     * 2. 数据面(框架运行): ngx_http_core_run_phases
     */
    if (ngx_http_init_phase_handlers(cf, cmcf) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    /* optimize the lists of ports, addresses and server names */
    //初始化socket相关的东西
    //!< 遇到listen命令时, ngx_http_core_listen -> ngx_http_add_listen -> ngx_http_add_address 已初始化了cmcf->ports
    /** 对于虚拟主机场景(ip:port相同, 但src_conf创建不同server_name 对应的不同ngx_http_core_srv_conf_t)
     * 1. 创建server_name哈希表
     *      ngx_http_optimize_servers -> ngx_http_server_names 为不同server_name创建相应的哈希表(匹配优先级由高到低: 普通 > 前置通配符 > 后置通配符)
     * 2. 虚拟主机匹配
     *      在 Nginx 处理请求过程中，在函数 ngx_http_find_virtual_server 中，根据请求包头 的 “Host” 字段内容，
     *      使用 ngx_hash_find_combined 函数对虚拟主机名哈希表中进行 查找匹配，寻找合适的虚拟主机
     */
    if (ngx_http_optimize_servers(cf, cmcf, cmcf->ports) != NGX_OK) {   
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;

failed:

    *cf = pcf;

    return rv;
}

/**
 * 初始化 http请求处理的11 Phase 容器
 */
static ngx_int_t ngx_http_init_phases(ngx_conf_t *cf, ngx_http_core_main_conf_t *cmcf)
{
    if (ngx_array_init(&cmcf->phases[NGX_HTTP_POST_READ_PHASE].handlers,
                       cf->pool, 1, sizeof(ngx_http_handler_pt))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_array_init(&cmcf->phases[NGX_HTTP_SERVER_REWRITE_PHASE].handlers,
                       cf->pool, 1, sizeof(ngx_http_handler_pt))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_array_init(&cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers,
                       cf->pool, 1, sizeof(ngx_http_handler_pt))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_array_init(&cmcf->phases[NGX_HTTP_PREACCESS_PHASE].handlers,
                       cf->pool, 1, sizeof(ngx_http_handler_pt))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_array_init(&cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers,
                       cf->pool, 2, sizeof(ngx_http_handler_pt))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_array_init(&cmcf->phases[NGX_HTTP_CONTENT_PHASE].handlers,
                       cf->pool, 4, sizeof(ngx_http_handler_pt))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_array_init(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers,
                       cf->pool, 1, sizeof(ngx_http_handler_pt))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}

//!< 初始化 http请求头哈希表, e.g. ngx_http_headers_in
static ngx_int_t
ngx_http_init_headers_in_hash(ngx_conf_t *cf, ngx_http_core_main_conf_t *cmcf)
{
    ngx_array_t         headers_in;
    ngx_hash_key_t     *hk;
    ngx_hash_init_t     hash;
    ngx_http_header_t  *header;

    if (ngx_array_init(&headers_in, cf->temp_pool, 32, sizeof(ngx_hash_key_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    for (header = ngx_http_headers_in; header->name.len; header++) {
        hk = ngx_array_push(&headers_in);
        if (hk == NULL) {
            return NGX_ERROR;
        }

        hk->key = header->name;
        hk->key_hash = ngx_hash_key_lc(header->name.data, header->name.len);
        hk->value = header;
    }

    hash.hash = &cmcf->headers_in_hash;
    hash.key = ngx_hash_key_lc;
    hash.max_size = 512;
    hash.bucket_size = ngx_align(64, ngx_cacheline_size);
    hash.name = "headers_in_hash";
    hash.pool = cf->pool;
    hash.temp_pool = NULL;

    if (ngx_hash_init(&hash, headers_in.elts, headers_in.nelts) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

/** 该方法的主要作用是把注册到cmcf->phases[i].handlers中的方法, 放到cmcf->phase_engine.handlers阶段引擎中
 * ngx本身的模块或者是第三方模块, 如果要介入到请求操作中, 就需要把方法注册到每个阶段代表的handler数组中, 而cmcf->phases[i].handlers就是那个数组
 *
 * 从该方法的代码逻辑可知
 *  NGX_HTTP_FIND_CONFIG_PHASE
 *  NGX_HTTP_POST_REWRITE_PHASE
 *  NGX_HTTP_POST_ACCESS_PHASE
 *  NGX_HTTP_TRY_FILES_PHASE
 * 以上四个阶段的执行handler方法是固定的,第三方模块无法接入
 */
static ngx_int_t ngx_http_init_phase_handlers(ngx_conf_t *cf, ngx_http_core_main_conf_t *cmcf)
{
    ngx_int_t                   j;
    ngx_uint_t                  i, n;
    ngx_uint_t                  find_config_index, use_rewrite, use_access;
    ngx_http_handler_pt        *h;
    ngx_http_phase_handler_t   *ph;
    ngx_http_phase_handler_pt   checker;

    cmcf->phase_engine.server_rewrite_index = (ngx_uint_t) -1;
    cmcf->phase_engine.location_rewrite_index = (ngx_uint_t) -1;
    find_config_index = 0;  //!< NGX_HTTP_FIND_CONFIG_PHASE阶段在脚本引擎cmcf->phase_engine.handlers中的开始索引

    /** 如果NGX_HTTP_REWRITE_PHASE阶段注册了方法,则说明使用了rewrite阶段
     * server rewrite handler   == ngx_http_rewrite_handler
     * location rewrite handler == ngx_http_rewrite_handler
     */
    use_rewrite = cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers.nelts ? 1 : 0;
    /** 确定是否在NGX_HTTP_ACCESS_PHASE阶段注册了方法
     * 访问控制模块handler = ngx_http_access_handler 
     */
    use_access = cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers.nelts ? 1 : 0;

    /** 判断ngx.conf中是否用了四个内置封闭(不可介入模块)的指令, 若使用了就需要把相应阶段加到引擎容器里(cmcf->phase_engine.handlers)
     * 因为有下面四个阶段
     *      NGX_HTTP_FIND_CONFIG_PHASE
     *      NGX_HTTP_POST_REWRITE_PHASE
     *      NGX_HTTP_POST_ACCESS_PHASE
     *      NGX_HTTP_TRY_FILES_PHASE
     * 是不允许被其它模块介入的, 但是他们也是在脚本引擎中被执行的, 所以需要在脚本引擎的handlers中(cmcf->phase_engine.handlers)
     * 分配一个ngx_http_phase_handler_t来执行对应的checker, 从下面的循环代码可以看到,计算脚本引擎的
     * handlers中ngx_http_phase_handler_t的个数用的是cmcf->phases[i].handlers.nelts, 也就是每个阶段注册方法个数的总和,
     * 这里并没有包含上面四个阶段,因为四个阶段是不允许被介入的, 所以他们对应的注册方法个数也就是0, 下面的这个表达式就是把他们加上
     *
     * use_rewrite: 代表使用了NGX_HTTP_REWRITE_PHASE阶段,那么后续就需要NGX_HTTP_POST_REWRITE_PHASE阶段来做rewrite操作
     * use_access: 代表使用了NGX_HTTP_ACCESS_PHASE阶段,那么后续就需要NGX_HTTP_POST_ACCESS_PHASE阶段在做访问控制操作
     * cmcf->try_files: 使用了NGX_HTTP_TRY_FILES_PHASE阶段
     * 1: NGX_HTTP_FIND_CONFIG_PHASE阶段是必须使用的,所以固定写1
     */
    n = use_rewrite + use_access + cmcf->try_files + 1 /* find config phase */;

    /** 计算所有handler总和
     * 把cmcf->phases中所有注册的方法个数都加起来, 如果在对应的不可介入的阶段也注册了方法, 那么这些方法的个数也会加起来
     * 这里主要是为了计算需要分配多少个ngx_http_phase_handler_t结构体, 后续会根据这个个数来分配内存, 但并不一定所有的内存都会被用到
     * 如果在不可介入的阶段也注册了方法, 那么有几个多余的方法就会产生几个多余的内存
     */
    for (i = 0; i < NGX_HTTP_LOG_PHASE; i++) {
        n += cmcf->phases[i].handlers.nelts;
    }

    /** 为每一个方法(所有阶段中注册的)分配一个ngx_http_phase_handler_t结构体, 阶段引擎会用到这个结构体来执行相应阶段的方法
     * 如果use_rewrite、use_access、cmcf->try_files这三个都有值,那么最终n的值会比阶段中注册的所有方法个数大4,
     * 从实际分配的空间来看,会比实际多出四个ngx_http_phase_handler_t和一个指针的空间大小。
     *
     * 正常情况下cmcf->phase_engine.handlers数组中放的应该是ngx_http_phase_handler_t结构体空间的倍数
     * 但是这里最后且存放了一个指针空间, 目的其实很简单, 在cheker方法(ngx_http_core_content_phase)中判断是否是最后一个ph
     * 而判断的依据是ph->checker是否存在, 我们知道ph->checker正好是一个方法指针,所以这里最后多出的指针空间可以当做ngx_http_phase_handler_t结构体的checker字段占用的空间
     *
     * 如果最后不用一个指针空间, 而是用一个完整的ngx_http_phase_handler_t结构体, 这样该结构体中的另两个字段的空间
     * 其实是浪费的, 因为ngx判断ph是否是最后一个用的是checker字段, 而不是另外两个
     */
    ph = ngx_pcalloc(cf->pool, n * sizeof(ngx_http_phase_handler_t) + sizeof(void *));
    if (ph == NULL) {
        return NGX_ERROR;
    }
    cmcf->phase_engine.handlers = ph;   //!< 把分配好的ngx_http_phase_handler_t内存空间赋值给阶段引擎的handlers字段

    /**
     * n表示每个阶段的开始方法的逻辑序号(从0开始递增)
     */
    n = 0;

	/**
	 * 该循环的目的是为阶段引擎中的handlers(ngx_http_phase_handler_t)设置checker方法和阶段真正要执行的方法
	 * ph是一个数组(cmcf->phase_engine.handlers), 他包含了所有阶段中的方法
	 */  
    for (i = 0; i < NGX_HTTP_LOG_PHASE; i++) {
        h = cmcf->phases[i].handlers.elts;  //!< 每个ngx_http_phases下 不同子阶段的 回调函数 指针数组首地址

        switch (i) {
        case NGX_HTTP_SERVER_REWRITE_PHASE:
            if (cmcf->phase_engine.server_rewrite_index == (ngx_uint_t) -1) {
                cmcf->phase_engine.server_rewrite_index = n;	//!< NGX_HTTP_SERVER_REWRITE_PHASE阶段在阶段引擎中的开始索引
            }
            checker = ngx_http_core_rewrite_phase;

            break;

            /** 
             * 查找location阶段,不可介入	
             */
        case NGX_HTTP_FIND_CONFIG_PHASE:
            find_config_index = n;                              //!< 此时n就是该阶段在脚本引擎cmcf->phase_engine.handlers中的第一个ph

            ph->checker = ngx_http_core_find_config_phase;
            n++;                                                //!< 该阶段只需要一个ph,并且没有对应的ph->handler,所以该阶段对应的下一个阶段的开始偏移量加一就可以
            ph++;                                               //!< 下一个ph

			/**
             *  这里直接跳过为ph->handler赋值的逻辑,表示该阶段的handler方法是固定的
             */
            continue;

        case NGX_HTTP_REWRITE_PHASE:
            if (cmcf->phase_engine.location_rewrite_index == (ngx_uint_t) -1) {
                cmcf->phase_engine.location_rewrite_index = n;	//!< NGX_HTTP_REWRITE_PHASE阶段在阶段引擎中的开始索引
            }
            checker = ngx_http_core_rewrite_phase;

            break;

            /**
             * 不可介入
             */
        case NGX_HTTP_POST_REWRITE_PHASE:
            if (use_rewrite) {
                ph->checker = ngx_http_core_post_rewrite_phase;	//!< 使用了NGX_HTTP_REWRITE_PHASE阶段,所以NGX_HTTP_POST_REWRITE_PHASE阶段就需要占用一个ph
				/**
				 * 执行到NGX_HTTP_POST_REWRITE_PHASE阶段后, 如果需要重新匹配location
                 * 则ph->next就是NGX_HTTP_FIND_CONFIG_PHASE阶段在脚本引擎handlers数组中的开始索引
				 *
				 * r->uri_changed等于1表示需要重新匹配location
				 */
                ph->next = find_config_index;
                n++;
                ph++;
            }

            /**
             *  这里直接跳过为ph->handler赋值的逻辑,表示该阶段的handler方法是固定的
             */
            continue;

        case NGX_HTTP_ACCESS_PHASE:
            checker = ngx_http_core_access_phase;
            n++;                                                //!< 为NGX_HTTP_POST_ACCESS_PHASE阶段预留一个ph
            break;

            /**
             * 不可介入
             */
        case NGX_HTTP_POST_ACCESS_PHASE:
            if (use_access) {                                   //!< 使用了NGX_HTTP_ACCESS_PHASE阶段,所以就会用到NGX_HTTP_POST_ACCESS_PHASE阶段
                ph->checker = ngx_http_core_post_access_phase;
                ph->next = n;                                   //!< 这里的n已经在上面的case中为NGX_HTTP_POST_ACCESS_PHASE阶段预留了一个ph,所以n就是该阶段的下一个阶段偏移量
                ph++;
            }

            /**
             *  这里直接跳过为ph->handler赋值的逻辑,表示该阶段的handler方法是固定的
             */
            continue;

            /**
             * 不可介入
             */
        case NGX_HTTP_TRY_FILES_PHASE:
            if (cmcf->try_files) {
                ph->checker = ngx_http_core_try_files_phase;
                n++;                                            //!< 该阶段只需要一个ph,并且没有对应的ph->handler,所以该阶段对应的下一个阶段的开始偏移量加一就可以
                ph++;
            }

            /**
             *  这里直接跳过为ph->handler赋值的逻辑,表示该阶段的handler方法是固定的
             */
            continue;

        case NGX_HTTP_CONTENT_PHASE:
            checker = ngx_http_core_content_phase;  //!< call ngx_http_static_handler
            break;

        default:
            /**
             * generic phase checker, used by the post read and pre-access phases
             *
             * NGX_HTTP_POST_READ_PHASE
             * NGX_HTTP_PREACCESS_PHASE
             * 以上两个阶段的checker方法固定为ngx_http_core_generic_phase
             *
             * NGX_HTTP_LOG_PHASE阶段独立运行
             */
            checker = ngx_http_core_generic_phase;
        }

        /**
         * n表示下一个阶段的开始方法的逻辑序号(从0开始递增)
         */
        n += cmcf->phases[i].handlers.nelts;    //!< 下个阶段的处理序号

        /**
         * 依次初始化ngx_http_phases每个阶段 checker统一入口, 以及 初始化每个阶段下 所有子阶段的调用顺序
         * 同一个阶段, 后注册的模块先执行(模块的注册顺序同编译顺序, 在ngx_modules[]中)
         */
        for (j = cmcf->phases[i].handlers.nelts - 1; j >=0; j--) {
            ph->checker = checker;  //!< 初始化 当前phase(阶段i)下 http处理框架(checker), 由checker作为统一入口,调用相应handler并跳转到下一handler
            ph->handler = h[j];     //!< 初始化 当前phase(阶段i)下 的每一个handler 
            ph->next = n;           //!< 初始化 当前phase(阶段i)下一阶段 的处理序号
            ph++;                   //!< 迭代 当前phase下的 链入的不同handler
        }
    }   //!< for (i = 0; i < NGX_HTTP_LOG_PHASE; i++) {

    return NGX_OK;
}


static char *
ngx_http_merge_servers(ngx_conf_t *cf, ngx_http_core_main_conf_t *cmcf,
    ngx_http_module_t *module, ngx_uint_t ctx_index)
{
    char                        *rv;
    ngx_uint_t                   s;
    ngx_http_conf_ctx_t         *ctx, saved;
    ngx_http_core_loc_conf_t    *clcf;
    ngx_http_core_srv_conf_t   **cscfp;

    cscfp = cmcf->servers.elts;
    ctx = (ngx_http_conf_ctx_t *) cf->ctx;
    saved = *ctx;
    rv = NGX_CONF_OK;
    
    //遍历所有的server，然后判断模块是否有merge回调函数，如果有的话，就调用回调函数
    for (s = 0; s < cmcf->servers.nelts; s++) {

        /* merge the server{}s' srv_conf's */

        ctx->srv_conf = cscfp[s]->ctx->srv_conf;

        if (module->merge_srv_conf) {
            rv = module->merge_srv_conf(cf, saved.srv_conf[ctx_index],
                                        cscfp[s]->ctx->srv_conf[ctx_index]);
            if (rv != NGX_CONF_OK) {
                goto failed;
            }
        }

        if (module->merge_loc_conf) {

            /* merge the server{}'s loc_conf */

            ctx->loc_conf = cscfp[s]->ctx->loc_conf;

            rv = module->merge_loc_conf(cf, saved.loc_conf[ctx_index],
                                        cscfp[s]->ctx->loc_conf[ctx_index]);
            if (rv != NGX_CONF_OK) {
                goto failed;
            }

            /* merge the locations{}' loc_conf's */

            clcf = cscfp[s]->ctx->loc_conf[ngx_http_core_module.ctx_index];

            rv = ngx_http_merge_locations(cf, clcf->locations,
                                          cscfp[s]->ctx->loc_conf,
                                          module, ctx_index);
            if (rv != NGX_CONF_OK) {
                goto failed;
            }
        }
    }

failed:

    *ctx = saved;

    return rv;
}


static char *
ngx_http_merge_locations(ngx_conf_t *cf, ngx_queue_t *locations,
    void **loc_conf, ngx_http_module_t *module, ngx_uint_t ctx_index)
{
    char                       *rv;
    ngx_queue_t                *q;
    ngx_http_conf_ctx_t        *ctx, saved;
    ngx_http_core_loc_conf_t   *clcf;
    ngx_http_location_queue_t  *lq;

    if (locations == NULL) {
        return NGX_CONF_OK;
    }

    ctx = (ngx_http_conf_ctx_t *) cf->ctx;
    saved = *ctx;

    for (q = ngx_queue_head(locations);
         q != ngx_queue_sentinel(locations);
         q = ngx_queue_next(q))
    {
        lq = (ngx_http_location_queue_t *) q;

        clcf = lq->exact ? lq->exact : lq->inclusive;
        ctx->loc_conf = clcf->loc_conf;

        rv = module->merge_loc_conf(cf, loc_conf[ctx_index],
                                    clcf->loc_conf[ctx_index]);
        if (rv != NGX_CONF_OK) {
            return rv;
        }

        rv = ngx_http_merge_locations(cf, clcf->locations, clcf->loc_conf,
                                      module, ctx_index);
        if (rv != NGX_CONF_OK) {
            return rv;
        }
    }

    *ctx = saved;

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_init_locations(ngx_conf_t *cf, ngx_http_core_srv_conf_t *cscf/*各个srv_conf对应的ngx_http_core_srv_conf_t地址*/,
    ngx_http_core_loc_conf_t *pclcf/*srv_conf->loc_conf[0]*/)
{
    ngx_uint_t                   n;
    ngx_queue_t                 *q, *locations, *named, tail;
    ngx_http_core_loc_conf_t    *clcf;
    ngx_http_location_queue_t   *lq;        //!< nginx static location队列结构(横向(非共享前缀的字符串), 用queue相连;  纵向(前缀相同), 用list相连)
    ngx_http_core_loc_conf_t   **clcfp;
#if (NGX_PCRE)
    ngx_uint_t                   r;
    ngx_queue_t                 *regex;
#endif

    locations = pclcf->locations;

    if (locations == NULL) {    //!< 快速退出条件
        return NGX_OK;
    }

    /**
     * 对ngx_http_location_queue_t 组成的链表进行升序排序
     *
     *  排序完成后, 各类型的location_queue_t按照以下顺序排列
     *      1). 完全匹配, 前缀匹配(优先前缀匹配, 普通前缀匹配) << 正则匹配(按照配置文件的先后顺序, 从左至右排列) << 其他(noname << named) 
     *          (a). /abc    vs.   =/abc        ===> 长度一样时(strcmp==0), 完全匹配必须放在前面    
     *          (b). /abc    vs.   =/abcd       ===> 长度不一样时(strcmp<0), 只按照strcmp输出, 前缀匹配 在 完全匹配 前
     *      2). 同种类型内部, 按照strcmp 按照字母升序排列
     */
    ngx_queue_sort(locations, ngx_http_cmp_locations);

    named = NULL;
    n = 0;
#if (NGX_PCRE)
    regex = NULL;
    r = 0;
#endif

    /**
     * 迭代处理location嵌套情况 
     */
    for (q = ngx_queue_head(locations);
         q != ngx_queue_sentinel(locations);
         q = ngx_queue_next(q))
    {
        lq = (ngx_http_location_queue_t *) q;   //!< 得到 ngx_http_add_location创建的 ngx_http_location_queue_t, 其首地址为ngx_http_location_queue_t

        clcf = lq->exact ? lq->exact : lq->inclusive;

        /**
         * 处理location中嵌套location:
         * location {
         *      location {
         *
         *      }
         * }
         */
        if (ngx_http_init_locations(cf, NULL, clcf) != NGX_OK) {
            return NGX_ERROR;
        }

#if (NGX_PCRE)
        /**
         * 找到 第一个包含正则表达式的 ngx_http_location_queue_t节点, 因为已排序过了,所以找到此节点并 以此节点断开location链表
         */
        if (clcf->regex) {
            r++;

            if (regex == NULL) {
                regex = q;  //!< 找到 第一个包含正则表达式的 ngx_http_location_queue_t节点
            }

            continue;
        }
#endif

        if (clcf->named) {  //!< ngx_http_core_loc_conf_t    *clcf
            n++;

            if (named == NULL) {
                named = q;
            }

            continue;
        }

        if (clcf->noname) {
            break;
        }
    }   //!< for (q = ngx_queue_head(locations)); q != ngx_queue_sentinel(locations); q = ngx_queue_next(q) )

    if (q != ngx_queue_sentinel(locations)) {   //!< q为正则起始节点, 以此节点为界把location链表分割成两端: 前端(完全和前置匹配) 后端(正则匹配)
        ngx_queue_split(locations, q, &tail);
    }

    if (named) {
        clcfp = ngx_palloc(cf->pool,
                           (n + 1) * sizeof(ngx_http_core_loc_conf_t *));
        if (clcfp == NULL) {
            return NGX_ERROR;
        }

        cscf->named_locations = clcfp;

        for (q = named;
             q != ngx_queue_sentinel(locations);
             q = ngx_queue_next(q))
        {
            lq = (ngx_http_location_queue_t *) q;

            *(clcfp++) = lq->exact;
        }

        *clcfp = NULL;

        ngx_queue_split(locations, named, &tail);
    }

#if (NGX_PCRE)
    /**
     * 正则链表q,  lq = (ngx_http_location_queue_t *) q;
     * 将(ngx_http_core_loc_conf_t *)(lq->exact) 新建location的地址 存放在 父层容器pclcf->regex_locations二维数据组里
     */
    if (regex) {

        clcfp = ngx_palloc(cf->pool,
                           (r + 1) * sizeof(ngx_http_core_loc_conf_t *));
        if (clcfp == NULL) {
            return NGX_ERROR;
        }

        pclcf->regex_locations = clcfp;

        for (q = regex;
             q != ngx_queue_sentinel(locations);
             q = ngx_queue_next(q))
        {
            lq = (ngx_http_location_queue_t *) q;

            *(clcfp++) = lq->exact;
        }

        *clcfp = NULL;

        ngx_queue_split(locations, regex, &tail);
    }
#endif

    return NGX_OK;
}


static ngx_int_t
ngx_http_init_static_location_trees(ngx_conf_t *cf,
    ngx_http_core_loc_conf_t *pclcf)
{
    ngx_queue_t                *q, *locations;
    ngx_http_core_loc_conf_t   *clcf;
    ngx_http_location_queue_t  *lq;

    locations = pclcf->locations;

    if (locations == NULL) {            //!< 快速退出条件
        return NGX_OK;
    }

    if (ngx_queue_empty(locations)) {   //!< 快速退出条件
        return NGX_OK;
    }

    /**
     * 处理location{location{}} location嵌套情况
     */
    for (q = ngx_queue_head(locations);
         q != ngx_queue_sentinel(locations);
         q = ngx_queue_next(q))
    {
        lq = (ngx_http_location_queue_t *) q;

        clcf = lq->exact ? lq->exact : lq->inclusive;

        /**
         * 处理location中嵌套location:
         * location {
         *      location {
         *
         *      }
         * }
         */
        if (ngx_http_init_static_location_trees(cf, clcf) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    /** 一. 去重
     * 因为已升序排序, 所以只需对比相邻项是否相同即可
     */
    if (ngx_http_join_exact_locations(cf, locations) != NGX_OK) {
        return NGX_ERROR;
    }

    /** 二. 生成location list
     * 1. 前提:
     *      按照name从小到大顺序, 里面可能有相同前缀, 例如:  /aa, /aa1, /aa2, /ac, /b
     *
     *      排序完成后, 各类型的location_queue_t按照以下顺序排列
     *          1). 完全匹配 << 前缀匹配(优先前缀匹配, 普通前缀匹配) << 正则匹配(按照配置文件的先后顺序, 从左至右排列) << 其他(noname << named) 
     *          2). 同种类型内部, 按照strcmp 按照字母升序排列
     * 
     * 2. 作用: 
     *      将排序后(按照name从小到大顺序, 里面可能有相同前缀)的ngx_http_location_queue_t, 生成相应的location list队列
     * 
     * 3. 生成location list队列结果: 
     *
     *    lq->queue <---> aa <---> ac <---> b           <---> queue指针
     *                          
     *       lq->list <-> aa1 <-> aa2                     <-> list指针    
     *
     * ===========================================================================================================================================
     *      nginx 构建static location tree(三叉树), 见 http://blog.chinaunix.net/uid-27767798-id-3759557.html
     *					                                                                                                                                                    
     *		    ngx_http_location_queue_t                                                                                                                       
     *		                                                                                                                                                    
     *		                                                                                                                                                    
     *		                                                                                                                                                    
     *		            ┌─────┐     ┌────┐       ┌────┐     ┌────┐      ┌────┐      ┌────┐    ┌────┐     ┌────┐     ┌────┐    ┌────┐   ┌────┐   ┌────┐    ┌────┐
     *		  RawData:  │ a1  │────▶│ aa │ ────▶ │aac │ ──▶ │aad │────▶ │ ab │────▶ │abc │ ──▶│abd │────▶│abda│ ───▶│abe │───▶│ ac ├──▶│ ad │──▶│ada │───▶│ ae │
     *		            └─────┘◀────└────┘ ◀──── └────┘ ◀───└────┘◀─────└────┘◀──── └────┘◀───└────┘◀────└────┘ ◀───└────┘◀───└────┘◀──└────┘◀──┴────┘◀───└────┘
     *		                                                                                                                                                    
     *		                                                                                                                                                    
     *		                           ┌─────┬────┐      ┌────┐        ┌────┬────┬────┐               ║                                                         
     *		  After Transformation:    │ a1  │ aa │ ─────│ ab │────────┤ ac │ ad │ ae │               ║                                                         
     *		                           └─────┴──╦─┘      └──╦─┘        └────┴─╦──┴────┘               ║   ngx_http_location_queue_t->list pointer               
     *		                                    ║           ║                 ║                       ║                                                         
     *		                                    ║           ║                 ║                       ║                                                         
     *		                                    ║           ║               ┌─▼──┐                    ║                                                         
     *		                                 ┌──▼─┐      ┌──▼─┐             │ada │                    ▼     │                                                   
     *		                                 │aac │      │abc │             └────┘                          │                                                   
     *		                                 ├────┤      ├────┤     ┌────┐                                  │  ngx_http_location_queue_t->queue pointer         
     *		                                 │aad │      │abd ╠═════▶abda│                                  │                                                   
     *		                                 └────┘      ├────┤     └────┘                                  ▼                                                   
     *		                                             │abe │                                                                                                 
     *		                                             └────┘                                                                                                 
     */
    ngx_http_create_locations_list(locations/*链表头部*/, ngx_queue_head(locations)/*第一个有效元素*/);

    /** 三. 构造location tree三叉匹配数, 供数据面 ngx_http_core_find_config_phase -> ngx_http_core_find_location -> ngx_http_core_find_static_location 定位location{}块
     * 1. 作用:
     *      根据above location list, 递归生成 nginx 三叉树( 用于 http uri与location配置匹配 );
     * 2. 使用场景: 
     *      1).字符串精确匹配(location = /abc);
     *      2).字符前缀开头匹配(location ^~ /abc)
     */
    pclcf->static_locations = ngx_http_create_locations_tree(cf, locations, 0);
    if (pclcf->static_locations == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

/** 将当前loc_conf 链入到 parent_ctx(srv_conf) 对应的ngx_http_core_loc_conf_s->ngx_queue_t 双向链表表尾
 * @locations,  parent_ctx(srv_conf) 对应的ngx_http_core_loc_conf_s->ngx_queue_t 双向链表上
 * @clcf,       新分配的ctx->loc_conf[ngx_http_core_module.ctx_index]   
 */
ngx_int_t ngx_http_add_location(ngx_conf_t *cf, ngx_queue_t **locations/*父parent_ctx(srv_conf) 对应的ngx_http_core_loc_conf_s->ngx_queue_t双向链表*/,
    ngx_http_core_loc_conf_t *clcf)
{
    ngx_http_location_queue_t  *lq;

    if (*locations == NULL) {
        *locations = ngx_palloc(cf->temp_pool,
                                sizeof(ngx_http_location_queue_t));
        if (*locations == NULL) {
            return NGX_ERROR;
        }

        ngx_queue_init(*locations);
    }

    lq = ngx_palloc(cf->temp_pool, sizeof(ngx_http_location_queue_t));  //!< 分配 nginx location 队列结构
    if (lq == NULL) {
        return NGX_ERROR;
    }

    if (clcf->exact_match   /*字符串完全匹配*/
#if (NGX_PCRE)
        || clcf->regex      /*正则表达式*/
#endif
        || clcf->named/*location @ 内部跳转*/ || clcf->noname/*limit_except, Nginx通过limit_except后面指定的方法名来限制用户请求*/ )
    {
        lq->exact = clcf;           //!< 完全匹配 or 正则匹配
        lq->inclusive = NULL;       //!< inclusive 为空

    } else {                        //!< 其他场景: 前缀匹配
        lq->exact = NULL;
        lq->inclusive = clcf;       //!< inclusive 不为空
    }

    lq->name = &clcf->name;         //!< location = xxx(name的字符串)
    lq->file_name = cf->conf_file->file.name.data;
    lq->line = cf->conf_file->line;

    ngx_queue_init(&lq->list);

    ngx_queue_insert_tail(*locations, &lq->queue);

    return NGX_OK;
}

/**
 * 若one==two,  则返回零
 * 若one<two,   则返回负数
 * 若one>two,   则返回正数
 *
 * 排序动作: 小的放前面, 大的放后面
 *
 * 排序完成后, 各类型的location_queue_t按照以下顺序排列
 *      1. 完全匹配, 前缀匹配(优先前缀匹配, 普通前缀匹配) << 正则匹配(按照配置文件的先后顺序, 从左至右排列) << 其他(noname << named) 
 *          (1). /abc     vs.   =/abc       ===> 长度一样时(strcmp==0), 完全匹配必须放在前面    
 *          (2). /abc    vs.   =/abcd       ===> 长度不一样时(strcmp<0), 只按照strcmp输出, 前缀匹配 在 完全匹配 前
 *      2. 同种类型内部, 按照strcmp 按照字母升序排列
 */
static ngx_int_t ngx_http_cmp_locations(const ngx_queue_t *one, const ngx_queue_t *two)
{
    ngx_int_t                   rc;
    ngx_http_core_loc_conf_t   *first, *second;
    ngx_http_location_queue_t  *lq1, *lq2;

    lq1 = (ngx_http_location_queue_t *) one;
    lq2 = (ngx_http_location_queue_t *) two;

    first = lq1->exact ? lq1->exact : lq1->inclusive;
    second = lq2->exact ? lq2->exact : lq2->inclusive;

    if (first->noname && !second->noname) {         //!< noname(if(){} 对应的location)放在后面
        /* shift no named locations to the end */
        return 1;
    }

    if (!first->noname && second->noname) {
        /* shift no named locations to the end */
        return -1;
    }

    if (first->noname || second->noname) {          //!< 两个都是noname时, 不排序, 按照配置顺序排列
        /* do not sort no named locations */
        return 0;
    }

    if (first->named && !second->named) {           //!< named(内部跳转 auto redirect)放在后面
        /* shift named locations to the end */
        return 1;
    }

    if (!first->named && second->named) {
        /* shift named locations to the end */
        return -1;
    }

    if (first->named && second->named) {            //!< 都是named, 按照字符串比较顺序
        return ngx_strcmp(first->name.data, second->name.data);
    }

#if (NGX_PCRE)
    if (first->regex && !second->regex) {           //!< 正则放在后面
        /* shift the regex matches to the end */
        return 1;
    }

    if (!first->regex && second->regex) {
        /* shift the regex matches to the end */
        return -1;
    }

    if (first->regex || second->regex) {            //!< 两个都是正则, 不排序, 按照配置顺序排列
        /* do not sort the regex matches --- 不对正则排序, 故location在server块中的先后顺序很关键, 此先后顺序决定了 最终的匹配结果 */
        return 0;
    }
#endif

    rc = ngx_strcmp(first->name.data, second->name.data);   //!< strcmp字符串比较

    /**
     * 1. 到这里, 比较场景可包括以下任一
     *  (1). ^~ /abc  vs.   =/abc       ===> 长度一样时(rc==0), 完全匹配必须放在前面 
     *  (2). /abc     vs.   =/abc       ===> 长度一样时(rc==0), 完全匹配必须放在前面    
     *  (3). =/abc    vs.   ^~ /abc     ===> 长度一样时(rc==0), 完全匹配必须放在前面
     *  (4). =/abc    vs.   /abc        ===> 长度一样时(rc==0), 完全匹配必须放在前面
     *
     *  (5). ^~ /abc vs.   =/abcd       ===> 长度不一样时(rc<0), 只按照strcmp输出, 前缀匹配 在 完全匹配 前
     *  (6). /abc    vs.   =/abcd       ===> 长度不一样时(rc<0), 只按照strcmp输出, 前缀匹配 在 完全匹配 前
     *  (7). =/abcd    vs.   ^~ /abc    ===> 长度不一样时(rc>0), 只按照strcmp输出, 前缀匹配 在 完全匹配 前
     *  (8). =/abcd    vs.   /abc       ===> 长度不一样时(rc>0), 只按照strcmp输出, 前缀匹配 在 完全匹配 前
     *
     * 2. 注意: ngx_queue_sort->cmp(prev, q) 比较两个字符串时, cmp<=0时就跳出循环并发 q插入到prev后
     */
    if (rc == 0 && !first->exact_match && second->exact_match) {
        /** an exact match must be before the same inclusive one
         * 1. =/abc    vs.   /abc,  返回0即可, 因为ngx_queue_sort->cmp(prev, q) 比较两个字符串时, cmp<=0时就跳出循环并发 q插入到prev后, 即精确匹配肯定在前缀匹配前
         * 2. /abc     vs.   =/abc, 必须返回1, 这是当前if的逻辑
         */
        return 1;
    }

    return rc;
}


static ngx_int_t
ngx_http_join_exact_locations(ngx_conf_t *cf, ngx_queue_t *locations)
{
    ngx_queue_t                *q, *x;
    ngx_http_location_queue_t  *lq, *lx;

    q = ngx_queue_head(locations);

    while (q != ngx_queue_last(locations)) {

        x = ngx_queue_next(q);

        lq = (ngx_http_location_queue_t *) q;
        lx = (ngx_http_location_queue_t *) x;

        if (ngx_strcmp(lq->name->data, lx->name->data) == 0) {

            if ((lq->exact && lx->exact) || (lq->inclusive && lx->inclusive)) {
                ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                              "duplicate location \"%V\" in %s:%ui",
                              lx->name, lx->file_name, lx->line);

                return NGX_ERROR;
            }

            lq->inclusive = lx->inclusive;

            ngx_queue_remove(x);

            continue;
        }

        q = ngx_queue_next(q);
    }

    return NGX_OK;
}

/**
 * 1. 前提:
 *      按照name从小到大顺序, 里面可能有相同前缀, 例如:  /aa, /aa1, /aa2, /ac, /b
 * 
 * 2. 作用: 
 *      将排序后(按照name从小到大顺序, 里面可能有相同前缀)的ngx_http_location_queue_t, 生成相应的location list队列
 * 
 * 3. 生成location list队列结果: 
 *
 *    lq->queue <---> aa <---> ac <---> b           <---> queue指针
 *                          
 *       lq->list <-> aa1 <-> aa2                     <-> list指针    
 */
static void
ngx_http_create_locations_list(ngx_queue_t *locations/*链表头部*/, ngx_queue_t *q/*first element: locations->next*/)
{
    u_char                     *name;
    size_t                      len;
    ngx_queue_t                *x, tail;
    ngx_http_location_queue_t  *lq, *lx;

    /**
     * 如果location为空就没有必要继续走下面的流程了，尤其是递归到嵌套location
     */
    if (q == ngx_queue_last(locations)) {
        return;
    }

    lq = (ngx_http_location_queue_t *) q;

    /** 完全匹配的ngx_http_location_queue_t
     * 如果这个节点是精准匹配(inclusive==NULL), 那么这个节点就不会作为某些节点的前缀，不用拥有ngx_http_location_tree_node_t->tree节点
     * 适配场景: =/abc <===> /abcd, uri为/abcd
     * 则生成location tree为:
     *             =/abc
     *                  \
     *                   \ right右子树
     *                    \
     *                      /abcd
     * 当node走到=/abc对应的节点, 则下一步必须迭代node->right, 已找到/abcd对应的节点;
     *
     * 但注意, 这只意味着 完全匹配节点不能有tree(孩子)节点, 但不代表 完全匹配节点不能是其他节点tree(孩子)节点;
     *      例如以下场景: /abc <==> =/abcd <===> /abcd, 输入uri为/abcd
     *      则生成的location tree为: 
     *                          /abc
     *                            |
     *                            | tree节点
     *                            |
     *                          =/abcd
     *                                \
     *                                 \right节点
     *                                  \
     *                                 /abcd
     *      那么当node走到/abc对应的节点时, 要想完全匹配=/abcd则必须迭代当前node节点的后缀tree节点(因为=/abcd挂在node->tree上面)
     */
    if (lq->inclusive == NULL) {
        ngx_http_create_locations_list(locations, ngx_queue_next(q));
        return;
    }

    /** 前缀匹配场景
     * 以下为lq->inclusive不为空的情况, 即 前缀匹配情况, 将前缀相同的所有ngx_http_location_queue_t 连入首节点的list链表中 
     */

    len = lq->name->len;    //!< 获取共同前缀的长度, e.g. /aa /aa1 /aa2, 获取/aa的长度 
    name = lq->name->data;  //!< 共同前缀的数据

    /**
     * x指向 第一个 不与lq/q节点 拥有相同name前缀的 节点地址
     */
    for (x = ngx_queue_next(q);
         x != ngx_queue_sentinel(locations);
         x = ngx_queue_next(x))
    {
        lx = (ngx_http_location_queue_t *) x;

        if (len > lx->name->len
            || (ngx_strncmp(name, lx->name->data, len) != 0))
        {
            break;
        }
    }

    q = ngx_queue_next(q);

    if (q == x) {   //!< 如果q和x节点之间没有节点, 那么就没有必要递归后面了产生q节点的location list，直接递归q的后继节点x，产生x节点location list
        ngx_http_create_locations_list(locations, x);
        return;
    }

    /** ngx_queue_split分割后变为
     *   location  lq      tail    q          x
     *             aa             aa1 aa2     ac   b
     */
    ngx_queue_split(locations, q, &tail);   //!< location从q节点开始分割，那么现在location就是q节点之前的一段list

    /** ngx_queue_add拼接后变为
     *   location  lq                  tail    q          x
     *             aa            lq->list<--->aa1    aa2  ac   b
     */
    ngx_queue_add(&lq->list, &tail);        //!< q节点的list初始为从q节点开始到最后的一段list

    /**
     * 原则上因为需要递归两段list，一个为p的location list（从p.next到x.prev），另一段为x.next到location的最后一个元素
     * 这里如果x已经是location的最后一个了，那么就没有必要递归x.next到location的这一段了，因为这一段都是空的
     */
    if (x == ngx_queue_sentinel(locations)) {
        ngx_http_create_locations_list(&lq->list, ngx_queue_head(&lq->list));
        return;
    }

    /** split与add后变为
     *   location   lq       tail  x
     *              aa             ac   b
     *
     *       lq->list  q
     *                aa1 aa2
     */ 
    ngx_queue_split(&lq->list, x, &tail);
    ngx_queue_add(locations, &tail);

    /**
     * 递归生成lq->list对应的 location list队列
     */
    ngx_http_create_locations_list(&lq->list, ngx_queue_head(&lq->list));

    /**
     * 递归生成后半部(x到结尾节点) 对应的location list队列
     */
    ngx_http_create_locations_list(locations, x);
}


/**
 * to keep cache locality for left leaf nodes, allocate nodes in following
 * order: node, left subtree, right subtree, inclusive subtree(拥有相同前缀的子树)
 *
 * static location tree大大优化了精准匹配和前缀匹配的location的查找过程，线性递归查找效率低下
 * 三叉树的左节点代表当前比node节点的name小的节点，右节点代表比当前node节点name大的节点，tree节点表示拥有相同前缀的节点
 */
static ngx_http_location_tree_node_t *
ngx_http_create_locations_tree(ngx_conf_t *cf, ngx_queue_t *locations, size_t prefix/*已匹配过的 共同前缀 长度*/)
{
    size_t                          len;
    ngx_queue_t                    *q, tail;
    ngx_http_location_queue_t      *lq;
    ngx_http_location_tree_node_t  *node;

    /** 在横向兄弟节点方向上 取 "中间节点", 将locations分为两半; "中间节点" 作为树根 返回 
     * 1. 若链表个数为基数, 则返回中间节点即可
     * 2. 若链表个数为偶数, 则返回下半部的第一个项节点
     *
     * 做法
     *      两个链表节点mid, nxt
     *      每次mid走一步, nxt走两步
     *      当nxt到尾部时, 返回mid节点即可
     */
    q = ngx_queue_middle(locations);

    lq = (ngx_http_location_queue_t *) q;
    len = lq->name->len - prefix;           //!< 当前节点name的len 减去其前缀的长度

    node = ngx_palloc(cf->pool, offsetof(ngx_http_location_tree_node_t, name) + len);
    if (node == NULL) {
        return NULL;
    }

    node->left = NULL;
    node->right = NULL;
    node->tree = NULL;
    node->exact = lq->exact;
    node->inclusive = lq->inclusive;

    node->auto_redirect = (u_char) ((lq->exact && lq->exact->auto_redirect)
                           || (lq->inclusive && lq->inclusive->auto_redirect));

    node->len = (u_char) len;
    ngx_memcpy(node->name, &lq->name->data[prefix], len);   //!< 可以看到实际node的name是父节点的增量（不存储公共前缀，也许这是为了节省空间）

    ngx_queue_split(locations, q, &tail);                   //!< location队列是从头节点开始到q节点之前的节点，tail作为链表头指向的是 q节点到location最右节点的队列

    if (ngx_queue_empty(locations)) {                                   //!< 左右都为空: 兄弟节点为空, 已构建完 完全匹配对应的节点
        /** ngx_queue_split, 确保若左子树(location 到 q)为空, 则右子树可定也为空
         * ngx_queue_split() insures that if left part is empty,
         * then right one is empty too
         */
        goto inclusive;                                                 //!< 1. 若左字树为空, 则右子树可定也为空; 故可直接跳到 tree节点构造上了
    }

    node->left = ngx_http_create_locations_tree(cf, locations, prefix); //!<    左子树不为空时, 递归构建node的左节点
    if (node->left == NULL) {
        return NULL;
    }

    ngx_queue_remove(q);                                                //!<    递归构建完左子树后,  在location list中删除根节点(lq/q)

    if (ngx_queue_empty(&tail)) {                                       //!< 2. 若右字树为空, 构建tree节点
        goto inclusive;
    }

    node->right = ngx_http_create_locations_tree(cf, &tail, prefix);    //!<    右子树不为空, 递归构建node的右节点, 此时已剔除了根节点(lq/q了)
    if (node->right == NULL) {
        return NULL;
    }

inclusive:                                                              //!< 3. 递归构造node的tree指针节点(拥有相同前缀的子树)
    if (ngx_queue_empty(&lq->list)) {                                   //!<    若前缀节点为空, 说明没有前缀节点, 直接返回当前node即可
        return node;
    }

    node->tree = ngx_http_create_locations_tree(cf, &lq->list, prefix + len);
    if (node->tree == NULL) {
        return NULL;
    }

    return node;
}

/**
 * 向 ngx_http_block创建的 ngx_http_core_main_conf_t->ports (ngx_array_t)
 * 记录 lsopt 对应的 端口描述符(ngx_http_conf_port_t) 与 地址描述符(ngx_http_conf_addr_t)
 */
ngx_int_t ngx_http_add_listen(ngx_conf_t *cf/*cf->ctx 为ngx_http_core_server(srv_conf) 创建的ngx_http_conf_ctx_t*/,
        ngx_http_core_srv_conf_t *cscf/*srv_conf时创建的 ngx_http_core_srv_conf_t*/, ngx_http_listen_opt_t *lsopt)
{
    in_port_t                   p;
    ngx_uint_t                  i;
    struct sockaddr            *sa;
    struct sockaddr_in         *sin;
    ngx_http_conf_port_t       *port;   //!< 端口描述符
    ngx_http_core_main_conf_t  *cmcf;
#if (NGX_HAVE_INET6)
    struct sockaddr_in6        *sin6;
#endif

    /**
     * cmcf 为 ngx_http_block 为main_conf创建的 ngx_http_core_main_conf_t
     *
     * http main_conf ngx_http_core_main_conf_t->array(ngx_array_t)中 记录的所有 端口描述符(ngx_http_conf_port_t)
     * 而 ngx_http_conf_port_t->array (ngx_array_t) 中, 存放着所有引用当前端口描述的 地址描述符(ngx_http_conf_addr_t)
     */
    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    if (cmcf->ports == NULL) {
        cmcf->ports = ngx_array_create(cf->temp_pool, 2,
                                       sizeof(ngx_http_conf_port_t));
        if (cmcf->ports == NULL) {
            return NGX_ERROR;
        }
    }

    sa = &lsopt->u.sockaddr;

    switch (sa->sa_family) {

#if (NGX_HAVE_INET6)
    case AF_INET6:
        sin6 = &lsopt->u.sockaddr_in6;
        p = sin6->sin6_port;
        break;
#endif

#if (NGX_HAVE_UNIX_DOMAIN)
    case AF_UNIX:
        p = 0;
        break;
#endif

    default: /* AF_INET */
        sin = &lsopt->u.sockaddr_in;    //!< ip地址
        p = sin->sin_port;              //!< 端口
        break;
    }

    port = cmcf->ports->elts;
    
    /** 找到port一样的场景, 包括: 1)port一样,但ip地址不一样; 2)虚拟主机场景: port和ip都一样
     * 情况1. 虚拟主机场景: 端口描述符已存在, 地址描述符可以相同,但配置的server_name不同
     *      1. port相同, ip不同
     *      2. port相同, ip相同, server_name不同虚拟主机
     */
    for (i = 0; i < cmcf->ports->nelts; i++) {
        /**
         * 若main_conf中已有相同端口描述符, 则只需记录 地址描述符
         */
        if (p != port[i].port || sa->sa_family != port[i].family) {
            continue;
        }

        /* a port is already in the port list - 面向server_name 虚拟主机场景(即,端口和ip都一样,但server_name不一样) */
        return ngx_http_add_addresses(cf, cscf, &port[i], lsopt);   //!< 端口描述符已存在, 只需记录 地址描述符
    }

    /** 情况2. 端口描述符不存在时, 则需要新建一个
     * 此时, main_conf中 没有记录 "lsopt确定的地址和端口"
     * 则下面, 须向main_conf中 记录 端口描述符 和 地址描述符
     */
    /* add a port to the port list */
    /** 添加端口描述符
     * 在全局main_conf 中记录下 listen命令创建的 端口描述符(ngx_http_conf_port_t)
     */
    port = ngx_array_push(cmcf->ports);
    if (port == NULL) {
        return NGX_ERROR;
    }

    port->family = sa->sa_family;
    port->port = p;
    port->addrs.elts = NULL;

    /**
     * 在全局main_conf 中记录下 listen命令创建的 端口描述符(ngx_http_conf_port_t)
     */
    return ngx_http_add_address(cf/*cf->ctx 为ngx_http_core_server(srv_conf) 创建的ngx_http_conf_ctx_t*/, cscf/*srv_conf时创建的 ngx_http_core_srv_conf_t*/, port, lsopt);
}


static ngx_int_t
ngx_http_add_addresses(ngx_conf_t *cf, ngx_http_core_srv_conf_t *cscf,
    ngx_http_conf_port_t *port, ngx_http_listen_opt_t *lsopt)
{
    u_char                *p;
    size_t                 len, off;
    ngx_uint_t             i, default_server;
    struct sockaddr       *sa;
    ngx_http_conf_addr_t  *addr;
#if (NGX_HAVE_UNIX_DOMAIN)
    struct sockaddr_un    *saun;
#endif
#if (NGX_HTTP_SSL)
    ngx_uint_t             ssl;
#endif

    /*
     * we cannot compare whole sockaddr struct's as kernel
     * may fill some fields in inherited sockaddr struct's
     */

    sa = &lsopt->u.sockaddr;

    switch (sa->sa_family) {

#if (NGX_HAVE_INET6)
    case AF_INET6:
        off = offsetof(struct sockaddr_in6, sin6_addr);
        len = 16;
        break;
#endif

#if (NGX_HAVE_UNIX_DOMAIN)
    case AF_UNIX:
        off = offsetof(struct sockaddr_un, sun_path);
        len = sizeof(saun->sun_path);
        break;
#endif

    default: /* AF_INET */
        off = offsetof(struct sockaddr_in, sin_addr);   //!< ipv4地址
        len = 4;                                        //!< 地址长度
        break;
    }

    p = lsopt->u.sockaddr_data + off;

    addr = port->addrs.elts;

    /**
     * 情况1. port, ip 都相同
     */
    for (i = 0; i < port->addrs.nelts; i++) {

        /** for循环中 要解决ip:port都一样的情况
         * 若ip地址也相同(server_name虚拟主机不同), 则 跳到 ngx_http_add_server
         */
        if (ngx_memcmp(p, addr[i].opt.u.sockaddr_data + off, len) != 0) {
            continue;
        }

        /** 情况1.ip:port都相同场景
         * the address is already in the address list --- ip地址相同
         * 此种场景(ip:port相同), 可能是server_name不同, 现将cscf 加入到ngx_http_conf_addr_t->servers容器中
         */
        if (ngx_http_add_server(cf, cscf, &addr[i]) != NGX_OK) {
            return NGX_ERROR;
        }

        /** 确定default_server: 解决port->addrs.elts 与 当前lsopt default_server 冲突情况
         * preserve default_server bit during listen options overwriting
         */
        default_server = addr[i].opt.default_server;

#if (NGX_HTTP_SSL)
        ssl = lsopt->ssl || addr[i].opt.ssl;
#endif

        if (lsopt->set) {

            if (addr[i].opt.set) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                        "duplicate listen options for %s", addr[i].opt.addr);
                return NGX_ERROR;
            }

            addr[i].opt = *lsopt;
        }

        /* check the duplicate "default" server for this address:port */
        //!< 两个 ngx_http_listen_opt_t  同时设置了 default server
        if (lsopt->default_server) {

            if (default_server) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                        "a duplicate default server for %s", addr[i].opt.addr);
                return NGX_ERROR;
            }

            default_server = 1;
            addr[i].default_server = cscf;
        }

        addr[i].opt.default_server = default_server;
#if (NGX_HTTP_SSL)
        addr[i].opt.ssl = ssl;
#endif

        return NGX_OK;
    }

    /* add the address to the addresses list that bound to this port */
    //!< 场景2. port一样, 但ip不一样
    return ngx_http_add_address(cf, cscf, port, lsopt);
}


/*
 * add the server address, the server names and the server core module
 * configurations to the port list
 */

static ngx_int_t
ngx_http_add_address(ngx_conf_t *cf/*cf->ctx 为ngx_http_core_server(srv_conf) 创建的ngx_http_conf_ctx_t*/,
        ngx_http_core_srv_conf_t *cscf/*srv_conf时创建的 ngx_http_core_srv_conf_t*/, ngx_http_conf_port_t *port, ngx_http_listen_opt_t *lsopt)
{
    ngx_http_conf_addr_t  *addr;    //!< 地址描述符

    if (port->addrs.elts == NULL) {
        if (ngx_array_init(&port->addrs, cf->temp_pool, 4,
                           sizeof(ngx_http_conf_addr_t))
            != NGX_OK)
        {
            return NGX_ERROR;
        }
    }

    addr = ngx_array_push(&port->addrs);
    if (addr == NULL) {
        return NGX_ERROR;
    }

    addr->opt = *lsopt;
    addr->hash.buckets = NULL;
    addr->hash.size = 0;            //!< server_name 完全匹配哈希表
    addr->wc_head = NULL;           //!< server_name 前缀通配符哈希表
    addr->wc_tail = NULL;           //!< server_name 后缀通配符哈希表
#if (NGX_PCRE)
    addr->nregex = 0;
    addr->regex = NULL;             //!< server_name 正则匹配
#endif
    addr->default_server = cscf;    //!< 设置default_server, cscf指向 srv_conf时创建的 ngx_http_core_srv_conf_t
    addr->servers.elts = NULL;

    /**
     * 完成 ngx_http_conf_addr_t->servers 与 不同 ngx_http_core_srv_conf_t 映射关系
     * 以满足 相同ip:port, 不同servername虚拟主机的配置场景
     */
    return ngx_http_add_server(cf/*cf->ctx 为ngx_http_core_server(srv_conf) 创建的ngx_http_conf_ctx_t*/, cscf/*srv_conf时创建的 ngx_http_core_srv_conf_t*/, addr);
}


/* add the server core module configuration to the address:port */

static ngx_int_t
ngx_http_add_server(ngx_conf_t *cf/*cf->ctx 为ngx_http_core_server(srv_conf) 创建的ngx_http_conf_ctx_t*/,
        ngx_http_core_srv_conf_t *cscf/*srv_conf时创建的 ngx_http_core_srv_conf_t*/, ngx_http_conf_addr_t *addr)
{
    ngx_uint_t                  i;
    ngx_http_core_srv_conf_t  **server;

    if (addr->servers.elts == NULL) {
        if (ngx_array_init(&addr->servers, cf->temp_pool, 4,
                           sizeof(ngx_http_core_srv_conf_t *))
            != NGX_OK)
        {
            return NGX_ERROR;
        }

    } else {
        server = addr->servers.elts;
        for (i = 0; i < addr->servers.nelts; i++) {
            if (server[i] == cscf) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "a duplicate listen %s", addr->opt.addr);
                return NGX_ERROR;
            }
        }
    }

    /** 虚拟主机
     * 地址描述符的servers数据 下挂 相同(port与ip)的 不同srv_conf对应的ngx_http_core_srv_conf_t
     */
    server = ngx_array_push(&addr->servers);
    if (server == NULL) {
        return NGX_ERROR;
    }

    *server = cscf; //!< 记录下 对应的 srv_conf时创建的 ngx_http_core_srv_conf_t

    return NGX_OK;
}

/** 对于虚拟主机场景(ip:port相同, 但src_conf创建不同server_name 对应的不同ngx_http_core_srv_conf_t)
 * 1. 创建server_name哈希表
 *      ngx_http_optimize_servers -> ngx_http_server_names 为不同server_name创建相应的哈希表(匹配优先级由高到低: 普通 > 前置通配符 > 后置通配符)
 * 2. 虚拟主机匹配
 *      在 Nginx 处理请求过程中，在函数 ngx_http_find_virtual_server 中，根据请求包头 的 “Host” 字段内容，
 *      使用 ngx_hash_find_combined 函数对虚拟主机名哈希表中进行 查找匹配，寻找合适的虚拟主机
 */
static ngx_int_t ngx_http_optimize_servers(ngx_conf_t *cf, ngx_http_core_main_conf_t *cmcf,
        ngx_array_t *ports/*listen命令时, ngx_http_add_address 已初始化了ports*/)
{
    ngx_uint_t             p, a;
    ngx_http_conf_port_t  *port;
    ngx_http_conf_addr_t  *addr;

    if (ports == NULL) {
        return NGX_OK;
    }

    port = ports->elts;
    for (p = 0; p < ports->nelts; p++) {            //!< 1. iterate on 端口描述符

        ngx_sort(port[p].addrs.elts, (size_t) port[p].addrs.nelts,
                 sizeof(ngx_http_conf_addr_t), ngx_http_cmp_conf_addrs);    //!< 对同一端口下的 所有地址描述符排序

        /*
         * check whether all name-based servers have the same
         * configuraiton as a default server for given address:port
         */
        /** 对于虚拟主机场景(ip:port相同, 但src_conf创建不同server_name 对应的不同ngx_http_core_srv_conf_t)
         * 1. 创建server_name哈希表
         *      ngx_http_optimize_servers 为不同server_name创建相应的哈希表(普通, 前置wildcard, 后置wildcard)
         * 2. 虚拟主机匹配
         *      在 Nginx 处理请求过程中，在函数 ngx_http_find_virtual_server 中，根据请求包头 的 “Host” 字段内容，
         *      使用 ngx_hash_find_combined 函数对虚拟主机名哈希表中进行 查找匹配，寻找合适的虚拟主机
         */
        addr = port[p].addrs.elts;
        for (a = 0; a < port[p].addrs.nelts; a++) { //!< 2. iterate on 端口描述符->地址描述符

            /**
             * ip与port相同时, 基于域名的虚拟主机场景(多个server{server_name xxx} server{...}), 则通过 ngx_http_server_names 建立server_name对应不同 哈希表;
             * 哈希表 <key, value> == <"server_name", address of ngx_http_core_srv_conf_t>
             */
            if (addr[a].servers.nelts > 1/*一个地址描述符下, 存在多个 ngx_http_core_srv_conf_t, 具体见 listen解析命令 ngx_http_core_listen -> ngx_http_add_listen*/
#if (NGX_PCRE)
                || addr[a].default_server->captures
#endif
               )
            {
                /** 基于域名的虚拟主机场景, 初始化 server_names哈希表(普通, 前置wilcard, 后置wildcard)
                 * 在 Nginx 配置解析和初始化过程中，虚拟主机名经配置指令回调函数 ngx_http_core_server_name 读取并存储后，
                 * 由函数 ngx_http_server_names 转换成 虚拟主机名哈希表结构
                 */
                if (ngx_http_server_names(cf, cmcf, &addr[a]) != NGX_OK) {
                    return NGX_ERROR;
                }
            }
        }

        //初始化listen结构 
        if (ngx_http_init_listening(cf, &port[p]) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}

/** 对于虚拟主机场景(ip:port相同, 但src_conf创建不同server_name 对应的不同ngx_http_core_srv_conf_t)
 * 1. 创建server_name哈希表
 *      ngx_http_optimize_servers 为不同server_name创建相应的哈希表(普通, 前置wildcard, 后置wildcard)
 * 2. 虚拟主机匹配
 *      在 Nginx 处理请求过程中，在函数 ngx_http_find_virtual_server 中，根据请求包头 的 “Host” 字段内容，
 *      使用 ngx_hash_find_combined 函数对虚拟主机名哈希表中进行 查找匹配，寻找合适的虚拟主机
 */
static ngx_int_t ngx_http_server_names(ngx_conf_t *cf, ngx_http_core_main_conf_t *cmcf,
    ngx_http_conf_addr_t *addr)
{
    ngx_int_t                   rc;
    ngx_uint_t                  n, s;
    ngx_hash_init_t             hash;
    ngx_hash_keys_arrays_t      ha;
    ngx_http_server_name_t     *name;   //!< {key: httpHost, value: ngx_http_core_srv_conf_t *}
    ngx_http_core_srv_conf_t  **cscfp;
#if (NGX_PCRE)
    ngx_uint_t                  regex, i;

    regex = 0;
#endif

    ngx_memzero(&ha, sizeof(ngx_hash_keys_arrays_t));

    ha.temp_pool = ngx_create_pool(16384, cf->log);
    if (ha.temp_pool == NULL) {
        return NGX_ERROR;
    }

    ha.pool = cf->pool;

    /** 为 完全匹配/前缀通配符/后缀统配符 初始化以下
     * 1. value(ngx_hash_key_t)存储数组
     * 2. key(ngx_str_t)存储哈希表
     */
    if (ngx_hash_keys_array_init(&ha, NGX_HASH_LARGE) != NGX_OK) {
        goto failed;
    }

    cscfp = addr->servers.elts;                 //!< 一个地址描述符下挂着 几个ngx_http_core_srv_conf_t对应的地址, 具体见listen解析命令 ngx_http_core_listen -> ngx_http_add_listen

    /** 迭代1. 多个server{配置块, 但listen ip:port都一样}: server{listen 1.1.1.1:80; server_name abc.com}  server{listen 1.1.1.1:80; server_name: efg.com}
     * 对同一个地址描述符下的 不同server{} ngx_http_core_srv_conf_t 进行迭代
     */
    for (s = 0; s < addr->servers.nelts; s++) {

        /** 同一个server{server_name abc.com, efg.com} 可以用server_name指令配置多个 虚拟主机名
         * cscfp[s] 指向一个 server{}配置块 对应的 ngx_http_core_srv_conf_t
         * 
         * 因为存在以下场景
         *      server  {
         *          listen 1.1.1.1:80;
         *          server_name www.baidu.com test.baidu.com;
         *      }
         *
         * cscfp[s]->server_names 用ngx_array_t 存储 <key, value> = <"server_names", ngx_http_server_name_t>, 此map的生成过程 具体见server_names解析指令 ngx_http_core_server_name 
         */
        name = cscfp[s]->server_names.elts;     //!< ngx_http_server_name_t *name;

        /** 
         * 迭代2. 迭代同一个server{server_name abc.com, efg.com;} 配置块中的 不同的server_name
         */
        for (n = 0; n < cscfp[s]->server_names.nelts; n++) {

#if (NGX_PCRE)
            if (name[n].regex) {
                regex++;
                continue;
            }
#endif

            /**
             * 根据server_names创建的 ngx_http_server_name_t, 初始化 哈希表构造描述符ngx_hash_keys_arrays_t ha, 通过ha最终构造出server_name哈希表
             * 其中, 可能会涉及到通配符的虚拟主机, 因此设置了NGX_HASH_WILDCARD_KEY
             *
             * convert  "*.example.com" to "com.example.\0"
             *      and ".example.com"  to "com.example\0"
             *      and "example.com.*" to "example.com\0"
             * 将转换后的字符串 保存在ngx_hash_keys_arrays_t ha.[keys | dns_wc_head | dns_wc_tail]中
             *
             * !!!!!!!!!!!!!!!!!!!!!!! Note: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
             *  A special wildcard name in the form “.example.org” can be used to match both the exact name “example.org” and the wildcard name “*.example.org”. 
             */
            rc = ngx_hash_add_key(&ha, &name[n].name/*key, server_name(.baidu.com)*/, name[n].server/*value, ngx_http_core_srv_conf_t* */,
                                  NGX_HASH_WILDCARD_KEY/*可能包括通配符*/);

            if (rc == NGX_ERROR) {
                return NGX_ERROR;
            }

            if (rc == NGX_DECLINED) {
                ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                              "invalid server name or wildcard \"%V\" on %s",
                              &name[n].name, addr->opt.addr);
                return NGX_ERROR;
            }

            if (rc == NGX_BUSY) {
                ngx_log_error(NGX_LOG_WARN, cf->log, 0,
                              "conflicting server name \"%V\" on %s, ignored",
                              &name[n].name, addr->opt.addr);
            }
        }
    }

    hash.key = ngx_hash_key_lc;                             //!< key的哈希函数
    hash.max_size = cmcf->server_names_hash_max_size;       //!< 最大桶数
    hash.bucket_size = cmcf->server_names_hash_bucket_size; //!< 每个桶的内存容量, 单位:字节
    hash.name = "server_names_hash";
    hash.pool = cf->pool;

    /** 1.完全匹配哈希表
     * 根据 ngx_hash_keys_arrays_t->keys 初始化 无通配符哈希表
     */
    if (ha.keys.nelts) {
        //!< ngx_hash_init_t hash
        //!< ngx_http_conf_addr_t *addr
        hash.hash = &addr->hash;//!< 复用地址描述符ngx_http_conf_addr_t.hash 
        hash.temp_pool = NULL;

        /**
         * server_names配置生成的ngx_array_t(<key, value>=<"server_names", ngx_http_core_srv_conf_t>) 编译在addr->hash 完全匹配哈希表中
         */
        if (ngx_hash_init(&hash, ha.keys.elts/*value 存储---ngx_hash_key_t*/, ha.keys.nelts) != NGX_OK) {
            goto failed;
        }
    }

    /** 2.前缀通配符 哈希表
     * 根据 ngx_hash_keys_arrays_t->dns_wc_head 初始化 前缀通配符哈希表
     */
    if (ha.dns_wc_head.nelts) {

        /**
         * 所有url with wildcard 按字母顺序 由小到大排序
         * 拥有相同单元的主机名依次紧邻, 具体见 ngx_http_cmp_dns_wildcards -> ngx_dns_strcmp
         */
        ngx_qsort(ha.dns_wc_head.elts, (size_t)ha.dns_wc_head.nelts, sizeof(ngx_hash_key_t), ngx_http_cmp_dns_wildcards);

        hash.hash = NULL;   //!< 通配符hash 需要动态分配
        hash.temp_pool = ha.temp_pool;

        /**
         * convert  "*.example.com" to "com.example.\0"
         *      and ".example.com"  to "com.example\0"
         *      and "example.com.*" to "example.com\0"
         * 将转换后的字符串 保存在ngx_hash_keys_arrays_t ha.[dns_wc_head | dns_wc_tail]中
         */
        if (ngx_hash_wildcard_init(&hash, ha.dns_wc_head.elts, ha.dns_wc_head.nelts) != NGX_OK) {
            goto failed;
        }

        /**
         * 动态生成的前缀哈希表, 存放在 ngx_http_conf_addr_t->wc_head地址上
         */
        addr->wc_head = (ngx_hash_wildcard_t *) hash.hash;
    }

    /** 3.后置通配符 哈希表
     * 根据 ngx_hash_keys_arrays_t->dns_wc_tail 初始化 后缀通配符哈希表
     */
    if (ha.dns_wc_tail.nelts) {

        ngx_qsort(ha.dns_wc_tail.elts, (size_t) ha.dns_wc_tail.nelts,
                  sizeof(ngx_hash_key_t), ngx_http_cmp_dns_wildcards);

        hash.hash = NULL;
        hash.temp_pool = ha.temp_pool;

        if (ngx_hash_wildcard_init(&hash, ha.dns_wc_tail.elts,
                                   ha.dns_wc_tail.nelts)
            != NGX_OK)
        {
            goto failed;
        }

        /**
         * 动态生成的后缀哈希表, 存放在 ngx_http_conf_addr_t->wc_tail地址上
         */
        addr->wc_tail = (ngx_hash_wildcard_t *) hash.hash;
    }

    ngx_destroy_pool(ha.temp_pool);

#if (NGX_PCRE)

    if (regex == 0) {
        return NGX_OK;
    }

    addr->nregex = regex;
    addr->regex = ngx_palloc(cf->pool, regex * sizeof(ngx_http_server_name_t));
    if (addr->regex == NULL) {
        return NGX_ERROR;
    }

    i = 0;

    for (s = 0; s < addr->servers.nelts; s++) {

        name = cscfp[s]->server_names.elts;

        for (n = 0; n < cscfp[s]->server_names.nelts; n++) {
            if (name[n].regex) {
                addr->regex[i++] = name[n];
            }
        }
    }

#endif

    return NGX_OK;

failed:

    ngx_destroy_pool(ha.temp_pool);

    return NGX_ERROR;
}


static ngx_int_t
ngx_http_cmp_conf_addrs(const void *one, const void *two)
{
    ngx_http_conf_addr_t  *first, *second;

    first = (ngx_http_conf_addr_t *) one;
    second = (ngx_http_conf_addr_t *) two;

    if (first->opt.wildcard) {
        /* a wildcard address must be the last resort, shift it to the end */
        return 1;
    }

    if (first->opt.bind && !second->opt.bind) {
        /* shift explicit bind()ed addresses to the start */
        return -1;
    }

    if (!first->opt.bind && second->opt.bind) {
        /* shift explicit bind()ed addresses to the start */
        return 1;
    }

    /* do not sort by default */

    return 0;
}


static int ngx_libc_cdecl
ngx_http_cmp_dns_wildcards(const void *one, const void *two)
{
    ngx_hash_key_t  *first, *second;

    first = (ngx_hash_key_t *) one;
    second = (ngx_hash_key_t *) two;

    return ngx_dns_strcmp(first->key.data, second->key.data);
}


/**
 * 监听端口初始化: listen, bind 
 */
static ngx_int_t
ngx_http_init_listening(ngx_conf_t *cf, ngx_http_conf_port_t *port)
{
    ngx_uint_t                 i, last, bind_wildcard;
    ngx_listening_t           *ls;
    ngx_http_port_t           *hport;
    ngx_http_conf_addr_t      *addr;

    addr = port->addrs.elts;
    last = port->addrs.nelts;

    /**
     * If there is a binding to an "*:port" then we need to bind() to the "*:port" only and ignore other implicit bindings.
     * The bindings have been already sorted: explicit bindings are on the start, then implicit bindings go, and wildcard binding is in the end.
     * 次序说明: 显示绑定 > 隐式绑定 > 通配符绑定
     */
    if (addr[last - 1].opt.wildcard) {
        addr[last - 1].opt.bind = 1;
        bind_wildcard = 1;

    } else {
        bind_wildcard = 0;
    }

    i = 0;

    while (i < last) {

        if (bind_wildcard && !addr[i].opt.bind) {
            i++;
            continue;
        }
        
        //这个函数里面将会创建，并且初始化listen结构
        ls = ngx_http_add_listening(cf, &addr[i]);  //!< 根据 listen时生成的ngx_http_conf_addr_t， 创建一个ngx_listening_t *ls
        if (ls == NULL) {
            return NGX_ERROR;
        }

        hport = ngx_pcalloc(cf->pool, sizeof(ngx_http_port_t));
        if (hport == NULL) {
            return NGX_ERROR;
        }

        ls->servers = hport;    //!< -----------------------  ngx_listening_t->servers 初始化

        if (i == last - 1) {
            hport->naddrs = last;

        } else {
            hport->naddrs = 1;
            i = 0;
        }

        switch (ls->sockaddr->sa_family) {

#if (NGX_HAVE_INET6)
        case AF_INET6:
            if (ngx_http_add_addrs6(cf, hport, addr) != NGX_OK) {
                return NGX_ERROR;
            }
            break;
#endif
        default: /* AF_INET */
            //初始化虚拟主机相关的地址，设置hash等等
            if (ngx_http_add_addrs(cf, hport, addr) != NGX_OK) {
                return NGX_ERROR;
            }
            break;
        }

        addr++;
        last--;
    }

    return NGX_OK;
}


static ngx_listening_t *
ngx_http_add_listening(ngx_conf_t *cf, ngx_http_conf_addr_t *addr)
{
    ngx_listening_t           *ls;
    ngx_http_core_loc_conf_t  *clcf;
    ngx_http_core_srv_conf_t  *cscf;

    //创建listen结构体
    ls = ngx_create_listening(cf, &addr->opt.u.sockaddr, addr->opt.socklen);
    if (ls == NULL) {
        return NULL;
    }

    ls->addr_ntop = 1;

    //设置listen句柄的回调,注意这个handler并不是accept handler，而是当accpet完之后，处理accept到的句柄的操作
    ls->handler = ngx_http_init_connection; //!< 每个listen监听事件回调

    cscf = addr->default_server;    //!< 获取 srv_conf时创建的 ngx_http_core_srv_conf_t
    ls->pool_size = cscf->connection_pool_size;
    ls->post_accept_timeout = cscf->client_header_timeout;

    clcf = cscf->ctx->loc_conf[ngx_http_core_module.ctx_index]; //!< cscf->ctx 为srv_conf 所属的ngx_http_conf_ctx_t

    ls->logp = clcf->error_log;
    ls->log.data = &ls->addr_text;
    ls->log.handler = ngx_accept_log_error;

#if (NGX_WIN32)
    {
    ngx_iocp_conf_t  *iocpcf = NULL;

    if (ngx_get_conf(cf->cycle->conf_ctx, ngx_events_module)) {
        iocpcf = ngx_event_get_conf(cf->cycle->conf_ctx, ngx_iocp_module);
    }
    if (iocpcf && iocpcf->acceptex_read) {
        ls->post_accept_buffer_size = cscf->client_header_buffer_size;
    }
    }
#endif
    
    //设置对应的属性,backlog,读写buf
    ls->backlog = addr->opt.backlog;
    ls->rcvbuf = addr->opt.rcvbuf;
    ls->sndbuf = addr->opt.sndbuf;

#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)
    ls->accept_filter = addr->opt.accept_filter;
#endif

#if (NGX_HAVE_DEFERRED_ACCEPT && defined TCP_DEFER_ACCEPT)
    ls->deferred_accept = addr->opt.deferred_accept;
#endif

#if (NGX_HAVE_INET6 && defined IPV6_V6ONLY)
    ls->ipv6only = addr->opt.ipv6only;
#endif

#if (NGX_HAVE_SETFIB)
    ls->setfib = addr->opt.setfib;
#endif

    return ls;
}


static ngx_int_t
ngx_http_add_addrs(ngx_conf_t *cf, ngx_http_port_t *hport,
    ngx_http_conf_addr_t *addr/*main_conf中的 ngx_http_core_main_conf_t->ngx_http_conf_port_t->addr*/)
{
    ngx_uint_t                 i;
    ngx_http_in_addr_t        *addrs;
    struct sockaddr_in        *sin;
    ngx_http_virtual_names_t  *vn;

    hport->addrs = ngx_pcalloc(cf->pool,
                               hport->naddrs * sizeof(ngx_http_in_addr_t));
    if (hport->addrs == NULL) {
        return NGX_ERROR;
    }

    addrs = hport->addrs;

    for (i = 0; i < hport->naddrs; i++) {

        sin = &addr[i].opt.u.sockaddr_in;
        addrs[i].addr = sin->sin_addr.s_addr;
        addrs[i].conf.default_server = addr[i].default_server;  //!< default_server
#if (NGX_HTTP_SSL)
        addrs[i].conf.ssl = addr[i].opt.ssl;
#endif

        if (addr[i].hash.buckets == NULL
            && (addr[i].wc_head == NULL
                || addr[i].wc_head->hash.buckets == NULL)
            && (addr[i].wc_tail == NULL
                || addr[i].wc_tail->hash.buckets == NULL)
#if (NGX_PCRE)
            && addr[i].nregex == 0
#endif
            )
        {
            continue;
        }

        vn = ngx_palloc(cf->pool, sizeof(ngx_http_virtual_names_t));
        if (vn == NULL) {
            return NGX_ERROR;
        }

        /**
         * 通过ngx_http_in_addr_t.conf.virtual_names, 供ngx_http_init_request 虚拟主机查找
         */
        addrs[i].conf.virtual_names = vn;

        vn->names.hash = addr[i].hash;
        vn->names.wc_head = addr[i].wc_head;
        vn->names.wc_tail = addr[i].wc_tail;
#if (NGX_PCRE)
        vn->nregex = addr[i].nregex;
        vn->regex = addr[i].regex;
#endif
    }

    return NGX_OK;
}


#if (NGX_HAVE_INET6)

static ngx_int_t
ngx_http_add_addrs6(ngx_conf_t *cf, ngx_http_port_t *hport,
    ngx_http_conf_addr_t *addr)
{
    ngx_uint_t                 i;
    ngx_http_in6_addr_t       *addrs6;
    struct sockaddr_in6       *sin6;
    ngx_http_virtual_names_t  *vn;

    hport->addrs = ngx_pcalloc(cf->pool,
                               hport->naddrs * sizeof(ngx_http_in6_addr_t));
    if (hport->addrs == NULL) {
        return NGX_ERROR;
    }

    addrs6 = hport->addrs;

    for (i = 0; i < hport->naddrs; i++) {

        sin6 = &addr[i].opt.u.sockaddr_in6;
        addrs6[i].addr6 = sin6->sin6_addr;
        addrs6[i].conf.default_server = addr[i].default_server;
#if (NGX_HTTP_SSL)
        addrs6[i].conf.ssl = addr[i].opt.ssl;
#endif

        if (addr[i].hash.buckets == NULL
            && (addr[i].wc_head == NULL
                || addr[i].wc_head->hash.buckets == NULL)
            && (addr[i].wc_tail == NULL
                || addr[i].wc_tail->hash.buckets == NULL)
#if (NGX_PCRE)
            && addr[i].nregex == 0
#endif
            )
        {
            continue;
        }

        vn = ngx_palloc(cf->pool, sizeof(ngx_http_virtual_names_t));
        if (vn == NULL) {
            return NGX_ERROR;
        }

        addrs6[i].conf.virtual_names = vn;

        vn->names.hash = addr[i].hash;
        vn->names.wc_head = addr[i].wc_head;
        vn->names.wc_tail = addr[i].wc_tail;
#if (NGX_PCRE)
        vn->nregex = addr[i].nregex;
        vn->regex = addr[i].regex;
#endif
    }

    return NGX_OK;
}

#endif


char *
ngx_http_types_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_array_t     **types;
    ngx_str_t        *value, *default_type;
    ngx_uint_t        i, n, hash;
    ngx_hash_key_t   *type;

    types = (ngx_array_t **) (p + cmd->offset);

    if (*types == (void *) -1) {
        return NGX_CONF_OK;
    }

    default_type = cmd->post;

    if (*types == NULL) {
        *types = ngx_array_create(cf->temp_pool, 1, sizeof(ngx_hash_key_t));
        if (*types == NULL) {
            return NGX_CONF_ERROR;
        }

        if (default_type) {
            type = ngx_array_push(*types);
            if (type == NULL) {
                return NGX_CONF_ERROR;
            }

            type->key = *default_type;
            type->key_hash = ngx_hash_key(default_type->data,
                                          default_type->len);
            type->value = (void *) 4;
        }
    }

    value = cf->args->elts;

    for (i = 1; i < cf->args->nelts; i++) {

        if (value[i].len == 1 && value[i].data[0] == '*') {
            *types = (void *) -1;
            return NGX_CONF_OK;
        }

        hash = ngx_hash_strlow(value[i].data, value[i].data, value[i].len);
        value[i].data[value[i].len] = '\0';

        type = (*types)->elts;
        for (n = 0; n < (*types)->nelts; n++) {

            if (ngx_strcmp(value[i].data, type[n].key.data) == 0) {
                ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                                   "duplicate MIME type \"%V\"", &value[i]);
                continue;
            }
        }

        type = ngx_array_push(*types);
        if (type == NULL) {
            return NGX_CONF_ERROR;
        }

        type->key = value[i];
        type->key_hash = hash;
        type->value = (void *) 4;
    }

    return NGX_CONF_OK;
}


char *
ngx_http_merge_types(ngx_conf_t *cf, ngx_array_t **keys, ngx_hash_t *types_hash,
    ngx_array_t **prev_keys, ngx_hash_t *prev_types_hash,
    ngx_str_t *default_types)
{
    ngx_hash_init_t  hash;

    if (*keys) {

        if (*keys == (void *) -1) {
            return NGX_CONF_OK;
        }

        hash.hash = types_hash;
        hash.key = NULL;
        hash.max_size = 2048;
        hash.bucket_size = 64;
        hash.name = "test_types_hash";
        hash.pool = cf->pool;
        hash.temp_pool = NULL;

        if (ngx_hash_init(&hash, (*keys)->elts, (*keys)->nelts) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        return NGX_CONF_OK;
    }

    if (prev_types_hash->buckets == NULL) {

        if (*prev_keys == NULL) {

            if (ngx_http_set_default_types(cf, prev_keys, default_types)
                != NGX_OK)
            {
                return NGX_CONF_ERROR;
            }

        } else if (*prev_keys == (void *) -1) {
            *keys = *prev_keys;
            return NGX_CONF_OK;
        }

        hash.hash = prev_types_hash;
        hash.key = NULL;
        hash.max_size = 2048;
        hash.bucket_size = 64;
        hash.name = "test_types_hash";
        hash.pool = cf->pool;
        hash.temp_pool = NULL;

        if (ngx_hash_init(&hash, (*prev_keys)->elts, (*prev_keys)->nelts)
            != NGX_OK)
        {
            return NGX_CONF_ERROR;
        }
    }

    *types_hash = *prev_types_hash;

    return NGX_CONF_OK;

}


ngx_int_t
ngx_http_set_default_types(ngx_conf_t *cf, ngx_array_t **types,
    ngx_str_t *default_type)
{
    ngx_hash_key_t  *type;

    *types = ngx_array_create(cf->temp_pool, 1, sizeof(ngx_hash_key_t));
    if (*types == NULL) {
        return NGX_ERROR;
    }

    while (default_type->len) {

        type = ngx_array_push(*types);
        if (type == NULL) {
            return NGX_ERROR;
        }

        type->key = *default_type;
        type->key_hash = ngx_hash_key(default_type->data,
                                      default_type->len);
        type->value = (void *) 4;

        default_type++;
    }

    return NGX_OK;
}
