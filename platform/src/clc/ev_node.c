/*
** Copyright (C) 2012 LuoYun Co. 
**
**           Authors:
**                    lijian.gnu@gmail.com 
**                    zengdongwu@hotmail.com
**  
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**  
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**  
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
**  
*/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../luoyun/luoyun.h"
#include "../util/logging.h"
#include "../util/lyxml.h"
#include "../util/lyutil.h"
#include "entity.h"
#include "lyjob.h"
#include "postgres.h"

/* node register and authtication */
static int __node_register_auth(NodeInfo * nf, int ent_id)
{
    if (nf->tag <= 0) {
        logerror(_("error in %s(%d)\n"), __func__, __LINE__);
        return -1;
    }

    int ret = -1;
    DBNodeRegInfo db_nf;
    bzero(&db_nf, sizeof(DBNodeRegInfo));

    int found = db_node_find(DB_NODE_FIND_BY_ID, &nf->tag, &db_nf);
    if (found == 1) {
        logdebug(_("tagged node found in db(%d %s %d %d)\n"),
                    db_nf.id, db_nf.ip, db_nf.status, db_nf.enabled);
        if (!ly_entity_is_authenticated(ent_id)) {
            logwarn(_("authentication is required for node(%d, %s)\n"),
                      nf->tag, nf->ip);
            goto out;
        }
        if (strcmp(nf->ip, db_nf.ip) != 0)
            logwarn(_("tagged node ip changed from %s to %s\n"),
                       db_nf.ip, nf->ip);
        ret = LY_S_REGISTERING_DONE_SUCCESS;
    }
    else if (found == 0) {
        logwarn(_("invalid tag for node(%d, %s), re-registration needed\n"),
                  nf->tag, nf->ip);
        ret = LY_S_REGISTERING_REINIT;
        goto out;
    }
    else {
        logerror(_("error in %s(%d)\n"), __func__, __LINE__);
        goto out;
    }

out:
    db_node_reginfo_free(&db_nf);
    return ret;
}

/* process node register request */
static int __node_xml_register(xmlDoc * doc, xmlNode * node, int ent_id)
{
    if (ly_entity_is_registered(ent_id)) {
        logwarn(_("received node register request again, ignored\n"));
        return -1;
    }

    NodeInfo *nf = ly_entity_data(ent_id);
    if (nf == NULL) {
        logerror(_("error in %s(%d)\n"), __func__, __LINE__);
        return -1;
    }

    /* Create xpath evaluation context */
    xmlXPathContextPtr xpathCtx = xmlXPathNewContext(doc);
    if (xpathCtx == NULL) {
        logerror(_("unable to create new XPath context %s, %d\n"),
                 __func__, __LINE__);
        return -1;
    }
    int ret = -1;

    char *str;
    str = xml_xpath_text_from_ctx(xpathCtx,
                         "/" LYXML_ROOT "/request/parameters/status");
    if (str == NULL)
        goto xml_err;
    nf->status = atoi(str);
    free(str);
    str = xml_xpath_text_from_ctx(xpathCtx,
                         "/" LYXML_ROOT "/request/parameters/host/tag");
    int tag = -1;
    if (str) {
        tag = atoi(str); /* NULL str is allowed for new node */
        free(str);
    }
    str = xml_xpath_text_from_ctx(xpathCtx,
                         "/" LYXML_ROOT "/request/parameters/host/name");
    if (str == NULL)
        goto xml_err;
    nf->hostname = str;
    str = xml_xpath_text_from_ctx(xpathCtx,
                         "/" LYXML_ROOT "/request/parameters/host/ip");
    if (str == NULL)
        goto xml_err;
    nf->ip = str;
    str = xml_xpath_text_from_ctx(xpathCtx,
                         "/" LYXML_ROOT "/request/parameters/arch");
    if (str == NULL)
        goto xml_err;
    nf->arch = atoi(str);
    free(str);
    str = xml_xpath_text_from_ctx(xpathCtx,
                         "/" LYXML_ROOT "/request/parameters/hypervisor");
    if (str == NULL)
        goto xml_err;
    nf->hypervisor = atoi(str);
    free(str);
    str = xml_xpath_text_from_ctx(xpathCtx,
                         "/" LYXML_ROOT "/request/parameters/network");
    if (str == NULL)
        goto xml_err;
    nf->network_type = atoi(str);
    free(str);
    str = xml_xpath_text_from_ctx(xpathCtx,
                         "/" LYXML_ROOT "/request/parameters/memory/total");
    if (str == NULL)
        goto xml_err;
    nf->max_memory = atoi(str);
    free(str);
    str = xml_xpath_text_from_ctx(xpathCtx,
                         "/" LYXML_ROOT "/request/parameters/memory/free");
    if (str == NULL)
        goto xml_err;
    nf->free_memory = atoi(str);
    free(str);
    str = xml_xpath_text_from_ctx(xpathCtx,
                         "/" LYXML_ROOT "/request/parameters/cpu/model");
    if (str == NULL)
        goto xml_err;
    nf->cpu_model = str;
    str = xml_xpath_text_from_ctx(xpathCtx,
                         "/" LYXML_ROOT "/request/parameters/cpu/mhz");
    if (str == NULL)
        goto xml_err;
    nf->cpu_mhz = atoi(str);
    free(str);
    str = xml_xpath_text_from_ctx(xpathCtx,
                         "/" LYXML_ROOT "/request/parameters/vcpu/max");
    if (str == NULL)
        goto xml_err;
    nf->max_cpus = atoi(str);
    free(str);
    str = xml_xpath_text_from_ctx(xpathCtx,
                          "/" LYXML_ROOT "/request/parameters/load/average");
    if (str == NULL)
        goto xml_err;
    nf->load_average = atoi(str);
    free(str);

    if (nf->status >= NODE_STATUS_REGISTERED) {
        logwarn(_("node(%d, %s) tries to register from wrong status(%d)\n"),
                 nf->tag, nf->ip, nf->status);
        ly_entity_release(ent_id);
        ret = 0; /* no need to continue node registration */
        goto done;
    }
        
    if (ly_entity_node_active(nf->ip)) {
        logwarn(_("duplicate node ip received. something is wrong...\n"));
        ly_entity_release(ent_id);
        ret = 0; /* no need to continue node registration */
        goto done;
    }
   
    if (tag > 0) {
        /* check authentication result */
        if (nf->tag > 0 && tag != nf->tag) {
            logerror(_("node tag changed, %d -> %d. something is wrong.\n"),
                       nf->tag, tag);
            goto done;
        }
        nf->tag = tag;
        ret = __node_register_auth(nf, ent_id);
        if (ret == LY_S_REGISTERING_DONE_SUCCESS) {
            AuthConfig * ac = ly_entity_auth(ent_id);
            if (db_node_update_secret(DB_NODE_FIND_BY_ID, &tag,
                                      ac->secret) < 0 ||
                db_node_update_status(DB_NODE_FIND_BY_ID, &tag,
                                      NODE_STATUS_REGISTERED) < 0) {
                logerror(_("error in %s(%d)\n"), __func__, __LINE__);
                ret = -1;
                goto done;
            }
            loginfo(_("node(tag:%d) registered\n"), tag);
            ly_entity_update(ent_id, tag, LY_ENTITY_FLAG_STATUS_REGISTERED);
        }
        goto done;
    }

    /* new node */
    DBNodeRegInfo db_nf;
    bzero(&db_nf, sizeof(DBNodeRegInfo));
    ret = db_node_find(DB_NODE_FIND_BY_IP, nf->ip, &db_nf);
    if (ret < 0 || ret > 1) {
        logerror(_("error in %s(%d)\n"), __func__, __LINE__);
        ret = -1;
        goto done;
    }

    if (ret == 0 || db_nf.secret) {
        /* new node */
        logdebug(_("new node\n"));
        if (nf->status != NODE_STATUS_UNINITIALIZED) {
            logwarn(_("node(%d, %s) tries to register from unexpected status(%d)\n"),
                       nf->tag, nf->ip, nf->status);
            nf->status = NODE_STATUS_UNINITIALIZED;
        }
        if (db_nf.secret)
            logwarn(_("new node takes ip(%s) used by tagged node\n"), nf->ip);
        ret = db_node_insert(nf);
        if (ret < 0) {
            logerror(_("error in %s(%d)\n"), __func__, __LINE__);
            goto new_done;
        }
        loginfo(_("new node %s added in db(%d)\n"), nf->ip, ret);
        ly_entity_update(ent_id, ret, LY_ENTITY_FLAG_STATUS_ONLINE);
        loginfo(_("new node %s added in db\n"), nf->ip);
        ret = LY_S_REGISTERING_INIT;
    }
    else {
        logdebug(_("untagged node for ip(%s) found in db\n"), nf->ip);
        if (db_nf.enabled) {
            AuthConfig * ac = ly_entity_auth(ent_id);
            ret = -1;
            if (ac->secret) {
                logerror(_("error in %s(%d)\n"), __func__, __LINE__);
                goto new_done;
            }
            ac->secret = lyauth_secret();
            if (ac->secret == NULL) {
                logerror(_("error in %s(%d)\n"), __func__, __LINE__);
                goto new_done;
            }
            nf->tag = db_nf.id;
            ret = LY_S_REGISTERING_CONFIG;
        }
        else {
            logdebug(_("untagged node %s register request done\n"), nf->ip);
            ret = LY_S_REGISTERING_INIT;
        }
        ly_entity_update(ent_id, db_nf.id, LY_ENTITY_FLAG_STATUS_ONLINE);
        if (db_node_update(DB_NODE_FIND_BY_ID, &db_nf.id, nf) < 0) {
            logerror(_("error in %s(%d)\n"), __func__, __LINE__);
            ret = -1;
        }
    }

new_done:
    db_node_reginfo_free(&db_nf);
    goto done;
xml_err:
    logerror(_("invalid node xml register request\n"));
done:
    xmlXPathFreeContext(xpathCtx);
    return ret;
}

/* process xml request */
static int __process_node_xml_request(xmlDoc * doc, xmlNode * node, int ent_id)
{
    loginfo(_("node request for entity %d\n"), ent_id);

    char *str = (char *) xmlGetProp(node, (const xmlChar *) "action");
    int action = atoi(str);
    if (action != LY_A_CLC_REGISTER_NODE) {
        logerror(_("clc received non-register node request.\n"));
        free(str);
        return -1;
    }
    free(str);

    loginfo(_("node register request\n"));

    str = (char *) xmlGetProp(node, (const xmlChar *) "id");
    if (str)
        logdebug("id = %s\n", str);
    else {
        logerror(_("error in %s(%d)\n"), __func__, __LINE__);
        return -1;
    }
    int id = atoi(str);
    free(str);

    int ret = __node_xml_register(doc, node, ent_id);
    if (ret < 0)
        logerror(_("node registeration failed.\n"));
    else if (ret == 0) {
        logdebug(_("node register not complete\n"));
        return 0;
    }

    NodeInfo * nf = ly_entity_data(ent_id);
    if (nf == NULL) {
        logerror(_("error in %s(%d)\n"), __func__, __LINE__);
        return -1;
    }

    AuthConfig * ac = ly_entity_auth(ent_id);

    AuthInfo ai;
    ai.tag = nf->tag > 0 ? nf->tag : 0;
    if (ac->secret && ret == LY_S_REGISTERING_CONFIG) {
        bzero(ai.data, LUOYUN_AUTH_DATA_LEN);
        int len = strlen(ac->secret);
        strncpy((char *)ai.data, ac->secret,
                len > LUOYUN_AUTH_DATA_LEN ?
                LUOYUN_AUTH_DATA_LEN : len);
    }
    else
        ai.data[0] = '\0';

    int fd = ly_entity_fd(ent_id);
    LYReply r;
    r.req_id = id;
    r.from = LY_ENTITY_CLC;
    r.to = LY_ENTITY_NODE;
    r.status = ret;
    r.data = &ai;
    char *response = lyxml_data_reply_auth_info(&r, NULL, 0);
    ret = ly_packet_send(fd, PKT_TYPE_NODE_REGISTER_REPLY,
                         response, strlen(response));
    free(response);

    return ret;
}

/*
** process instance info query reply
**
** the is the reply of instance info query through node request
*/
static int __instance_info_update(xmlDoc * doc, xmlNode * node,
                                  int ins_id, int * j_status)
{
    logdebug(_("%s called\n"), __func__);

    /* Create xpath evaluation context */
    xmlXPathContextPtr xpathCtx = xmlXPathNewContext(doc);
    if (xpathCtx == NULL) {
        logerror(_("unable to create new XPath context %s, %d\n"),
                 __func__, __LINE__);
        goto failed;
    }

    InstanceInfo ii;
    char *str;
    str = xml_xpath_text_from_ctx(xpathCtx,
                         "/" LYXML_ROOT "/response/data/status");
    if (str == NULL)
        goto failed;
    ii.status = atoi(str);
    free(str);
    ii.ip = xml_xpath_text_from_ctx(xpathCtx,
                         "/" LYXML_ROOT "/response/data/ip");

    logdebug(_("update info for instance %d: status %d, ip %s\n"),
                ins_id, ii.status, ii.ip);

    int ent_id = ly_entity_find_by_db(LY_ENTITY_OSM, ins_id);
    if (!ly_entity_is_registered(ent_id) &&
        db_instance_update_status(ins_id, &ii, -1) < 0) {
        logerror(_("error in %s(%d)\n"), __func__, __LINE__);
        if (ii.ip)
            free(ii.ip);
        goto failed;
    }

    if (ii.ip)
        free(ii.ip);
    return 0;

failed:
    *j_status = LY_S_FINISHED_FAILURE;
    return 0;
}

/* process node info query reply */
static int __node_info_update(xmlDoc * doc, xmlNode * node,
                              int ent_id, int * j_status)
{
    logdebug(_("%s called\n"), __func__);

    if (!ly_entity_is_online(ent_id)) {
        /* shouldn't come here */
        logerror(_("error in %s(%d)\n"), __func__, __LINE__);
        *j_status = LY_S_FINISHED_FAILURE_NODE_NOT_AVAIL;
        return 0;
    }

    NodeInfo *nf = ly_entity_data(ent_id);
    if (nf == NULL) {
        logerror(_("error in %s(%d)\n"), __func__, __LINE__);
        goto failed;
    }

    /* Create xpath evaluation context */
    xmlXPathContextPtr xpathCtx = xmlXPathNewContext(doc);
    if (xpathCtx == NULL) {
        logerror(_("unable to create new XPath context %s, %d\n"),
                 __func__, __LINE__);
        goto failed;
    }

    char *str;
    str = xml_xpath_text_from_ctx(xpathCtx,
                         "/" LYXML_ROOT "/response/data/status");
    if (str == NULL)
        goto failed;
    nf->status = atoi(str);
    free(str);

    str = xml_xpath_text_from_ctx(xpathCtx,
                         "/" LYXML_ROOT "/response/data/memory/free");
    if (str == NULL)
        goto failed;
    nf->free_memory = atoi(str);
    free(str);

    str = xml_xpath_text_from_ctx(xpathCtx,
                          "/" LYXML_ROOT "/response/data/load/average");
    if (str == NULL)
        goto failed;
    nf->load_average = atoi(str);
    free(str);

    int node_id = ly_entity_db_id(ent_id);
    logdebug(_("update info for node %d: %d %d %d\n"), node_id,
                nf->status, nf->free_memory, nf->load_average);

    if (db_node_update(DB_NODE_FIND_BY_ID, &node_id, nf) < 0) {
        logerror(_("error in %s(%d)\n"), __func__, __LINE__);
        goto failed;
    }

    /* no need to update j_status */
    return 0;

failed:
    *j_status = LY_S_FINISHED_FAILURE;
    return 0;
}

/* process xml response */
static int __process_node_xml_response(xmlDoc * doc, xmlNode * node,
                                       int ent_id)
{
    loginfo(_("node response for entity %d\n"), ent_id);
    char * str = (char *) xmlGetProp(node, (const xmlChar *) "id");
    if (str)
        logdebug("node response id(job id) = %s\n", str);
    else {
        logerror(_("error in %s(%d)\n"), __func__, __LINE__);
        return -1;
    }
    int id = atoi(str);
    free(str);

    str = (char *) xmlGetProp(node, (const xmlChar *) "status");
    if (str)
        logdebug("node response status = %s\n", str);
    else {
        logerror(_("error in %s(%d)\n"), __func__, __LINE__);
        return -1;
    }
    int status = atoi(str);
    free(str);

    node = node->children;
    for (; node; node = node->next) {
        if (node->type == XML_ELEMENT_NODE) {
            if (strcmp((char *)node->name, "result") == 0)
                break;
        }
    }
    if (node == NULL || node->children == NULL ||
        node->children->type != XML_TEXT_NODE) {
        logdebug(_("response no result message\n"));
    }
    else {
        node = node->children;
        char * result = strdup((char *)node->content);
        logdebug(_("response result: %s\n"), result);
    }

    LYJobInfo * job = job_find(id);
    if (job == NULL) {
        logwarn(_("job(%d) not found waiting for node reply\n"), id);
        return 0;
    }

    if (status == LY_S_FINISHED_SUCCESS &&
        job->j_action == LY_A_NODE_QUERY_INSTANCE) {
        if ( __instance_info_update(doc, node, job->j_target_id, &status)) {
            logerror(_("error in %s(%d)\n"), __func__, __LINE__);
            return -1;
        }
    }
   
    if (status == LY_S_FINISHED_SUCCESS &&
        job->j_action == LY_A_NODE_QUERY) {
        if ( __node_info_update(doc, node, ent_id, &status)) {
            logerror(_("error in %s(%d)\n"), __func__, __LINE__);
            return -1;
        }
    }
 
    if (job_update_status(job, status)) {
        logerror(_("error in %s(%d)\n"), __func__, __LINE__);
        return -1;
    }

    return 0;
}

/* process xml report */
static int __process_node_xml_report(xmlDoc * doc, xmlNode * node, int ent_id)
{
    loginfo(_("node report for entity %d\n"), ent_id);

    int node_id = ly_entity_db_id(ent_id);
    int status = -1;
    node = node->children;
    for (; node; node = node->next) {
        if (node->type == XML_ELEMENT_NODE) {
            if (strcmp((char *)node->name, "status") == 0) {
                if (node->children != NULL &&
                    node->children->type == XML_TEXT_NODE) {
                    status = atoi((char *)node->children->content);
                    logwarn(_("node %d report status %d\n"),
                               node_id, status);
                }
                else {
                    logerror(_("error in %s(%d)\n"), __func__, __LINE__);
                    return -1;
                }
            }
            else if (strcmp((char *)node->name, "message") == 0) {
                if (node->children != NULL && 
                    node->children->type == XML_TEXT_NODE) {
                    logwarn(_("node %d report message %s\n"),
                               node_id, node->children->content);
                }
            }
        }
    }

    NodeInfo *nf = ly_entity_data(ent_id);
    if (nf != NULL && status != -1) {
        nf->status = status;
        loginfo(_("update node status to %d from report\n"), status);
    }

    return 0;
}

/* process xml packet from node */
int eh_process_node_xml(char * xml, int ent_id)
{
    logdebug(_("%s called\n"), __func__);
    /* logdebug("%s\n", xml); */

    int ret = 0;
    xmlDoc *doc = xml_doc_from_str(xml);
    if (doc == NULL) {
        /* error: could not parse xml string */
        logerror(_("unrecognized node packet data\n%s\n"), xml);
        return -1;
    }
    xmlNode * node = xmlDocGetRootElement(doc);
    if (node == NULL || strcmp((char *)node->name, LYXML_ROOT) != 0) {
        /* error: xml string not for "LYXML_ROOT" */
        logerror(_("unrecognized node packet data\n%s\n"), xml);
        return -1;
    }

    node = node->children;

    for (; node; node = node->next) {
        if (node->type == XML_ELEMENT_NODE) {
            if (strcmp((char *)node->name, "response") == 0 ) {
                ret = __process_node_xml_response(doc, node, ent_id);
                if (ret  < 0)
                    break;
            }
            else if (strcmp((char *)node->name, "request") == 0 ) {
                ret = __process_node_xml_request(doc, node, ent_id);
                if (ret  < 0)
                    break;
            }
            else if (strcmp((char *)node->name, "report") == 0 ) {
                ret = __process_node_xml_report(doc, node, ent_id);
                if (ret  < 0)
                    break;
            }
            /* other nodes ignored */
        }
    }
    xmlFreeDoc(doc);
    return ret;
}

/* process raw auth request from node */
int eh_process_node_auth(int is_reply, void * data, int ent_id)
{
    logdebug(_("%s called\n"), __func__);

    int ret;
    AuthInfo * ai = data;
    AuthConfig * ac = ly_entity_auth(ent_id);

    if (is_reply) {
        loginfo(_("auth reply from node %d(tag)\n"), ai->tag);
        ret = lyauth_verify(ac, ai->data, LUOYUN_AUTH_DATA_LEN);
        if (ret < 0) {
            logerror(_("error in %s(%d)\n"), __func__, __LINE__);
            return -1;
        }
        if (ret) {
            loginfo(_("node %d(tag) is authenticated\n"), ai->tag);
            if (!ly_entity_is_authenticated(ent_id))
                ly_entity_update(ent_id, -1, LY_ENTITY_FLAG_STATUS_AUTHENTICATED);
        }
        else {
            logwarn(_("chanllenge verification for node %d(tag) failed.\n"),
                      ai->tag);
            return 1;
        }
        return 0;
    }

    loginfo(_("auth request from node %d(tag)\n"), ai->tag);

    NodeInfo * nf = ly_entity_data(ent_id);
    if (nf == NULL) {
        logerror(_("error in %s(%d)\n"), __func__, __LINE__);
        return -1;
    }

    /* get secret */
    if (ac->secret == NULL) {
        logdebug(_("retrieve auth key for node %d(tag)\n"), ai->tag);
        ret = db_node_find_secret(DB_NODE_FIND_BY_ID,
                                  &ai->tag,
                                  &ac->secret);
        if (ret < 0) {
            logerror(_("error in %s(%d)\n"), __func__, __LINE__);
            return -1;
        }
        else if (ret == 0) {
            logerror(_("node(tag: %d) not in db\n"), ai->tag);
            return -1;
        }
        nf->tag = ai->tag;
    }

    /* update node status */
    logdebug(_("update node status to %d\n"), NODE_STATUS_AUTHENTICATING);
    ret = db_node_update_status(DB_NODE_FIND_BY_ID, &ai->tag,
                                NODE_STATUS_AUTHENTICATING);
    if (ret < 0) {
        logerror(_("error in %s(%d)\n"), __func__, __LINE__);
        return -1;
    }

    /* resolve challenge */
    logdebug(_("answer auth request\n"));
    ret = lyauth_answer(ac, ai->data, LUOYUN_AUTH_DATA_LEN);
    if (ret < 0) {
        logerror(_("error in %s(%d)\n"), __func__, __LINE__);
        return -1;
    }

    /* send answer back */
    int fd = ly_entity_fd(ent_id);
    if (ly_packet_send(fd, PKT_TYPE_NODE_AUTH_REPLY,
                       ai, sizeof(AuthInfo)) < 0) {
        logerror(_("error in %s(%d)\n"), __func__, __LINE__);
        return -1;
    }

    /* request challenging */
    logdebug(_("clc sends out auth request\n"));
    if (lyauth_prepare(ac) < 0) {
        logerror(_("error in %s(%d)\n"), __func__, __LINE__);
        return -1;
    }

    bzero(ai->data, LUOYUN_AUTH_DATA_LEN);
    strncpy((char *)ai->data, ac->challenge, LUOYUN_AUTH_DATA_LEN);
    if (ly_packet_send(fd, PKT_TYPE_NODE_AUTH_REQUEST,
                       ai, sizeof(AuthInfo)) < 0) {
        logerror(_("error in %s(%d)\n"), __func__, __LINE__);
        return -1;
    }

    loginfo(_("node %d(tag) is online\n"), nf->tag);
    ly_entity_update(ent_id, nf->tag, LY_ENTITY_FLAG_STATUS_ONLINE);

    return 0;
}