
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HTTP_UPSTREAM_ROUND_ROBIN_H_INCLUDED_
#define _NGX_HTTP_UPSTREAM_ROUND_ROBIN_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/**
 * 每一个后端服务器用一个结构体ngx_http_upstream_rr_peer_t与之对应
 */
typedef struct {
    struct sockaddr                *sockaddr;       //!< 后端服务器地址
    socklen_t                       socklen;        //!< 后端服务器地址长度
    ngx_str_t                       name;           //!< 后端名称

    ngx_int_t                       current_weight; //!< 当前权重，nginx会在运行过程中调整此权重
    ngx_int_t                       weight;         //!< 配置的权重  

    ngx_uint_t                      fails;          //!< 已尝试失败次数
    time_t                          accessed;       //!< 检测失败时间，用于计算超时

    ngx_uint_t                      max_fails;      //!< 最大失败次数
    time_t                          fail_timeout;   //!< 多长时间内出现max_fails次失败便认为后端down掉了

    ngx_uint_t                      down;           //!< 指定某后端是否挂了 /* unsigned  down:1; */

#if (NGX_HTTP_SSL)
    ngx_ssl_session_t              *ssl_session;   /* local to a process */
#endif
} ngx_http_upstream_rr_peer_t;


typedef struct ngx_http_upstream_rr_peers_s  ngx_http_upstream_rr_peers_t;

/**
 * 表示一组后端服务器，比如一个后端集群
 */
struct ngx_http_upstream_rr_peers_s {
    ngx_uint_t                      single;         //!< 为1表示后端服务器总共只有一台，用于优化，此时不需要再做选择    /* unsigned  single:1; */
    ngx_uint_t                      number;         //!< 队列中服务器数量
    ngx_uint_t                      last_cached;

 /* ngx_mutex_t                    *mutex; */
    ngx_connection_t              **cached;

    ngx_str_t                      *name;

    ngx_http_upstream_rr_peers_t   *next;           //!< 后备服务器列表挂载在这个字段下

    ngx_http_upstream_rr_peer_t     peer[1];
};


typedef struct {
    ngx_http_upstream_rr_peers_t   *peers;
    ngx_uint_t                      current;
    uintptr_t                      *tried;
    uintptr_t                       data;
} ngx_http_upstream_rr_peer_data_t;


ngx_int_t ngx_http_upstream_init_round_robin(ngx_conf_t *cf,
    ngx_http_upstream_srv_conf_t *us);
ngx_int_t ngx_http_upstream_init_round_robin_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us);
ngx_int_t ngx_http_upstream_create_round_robin_peer(ngx_http_request_t *r,
    ngx_http_upstream_resolved_t *ur);
ngx_int_t ngx_http_upstream_get_round_robin_peer(ngx_peer_connection_t *pc,
    void *data);
void ngx_http_upstream_free_round_robin_peer(ngx_peer_connection_t *pc,
    void *data, ngx_uint_t state);

#if (NGX_HTTP_SSL)
ngx_int_t
    ngx_http_upstream_set_round_robin_peer_session(ngx_peer_connection_t *pc,
    void *data);
void ngx_http_upstream_save_round_robin_peer_session(ngx_peer_connection_t *pc,
    void *data);
#endif


#endif /* _NGX_HTTP_UPSTREAM_ROUND_ROBIN_H_INCLUDED_ */
