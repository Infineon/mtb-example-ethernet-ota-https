/******************************************************************************
* File Name: ota_task.c
*
* Description: This file contains task and functions related to OTA operation.
*
********************************************************************************
* Copyright 2024, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

/* Header file includes */
#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
/* Ethernet connection manager header files */
#include "cy_ecm.h"
#include "cy_ecm_error.h"
/* IoT SDK, Secure Sockets, and HTTP initialization */
#include "cy_tcpip_port_secure_sockets.h"
/* FreeRTOS */
#include <FreeRTOS.h>
#include <task.h>
/* OTA app specific configuration */
#include "ota_app_config.h"
/* OTA API */
#include "cy_ota_api.h"
/* OTA storage api */
#include "cy_ota_storage_api.h"
/* Ethernet PHY driver */
#include "cy_eth_phy_driver.h"
/*******************************************************************************
* Macros
********************************************************************************/
/* MAX connection retries to join Ethernet */
#define MAX_CONNECTION_RETRIES              (10)

/* Wait between connection retries */
#define ETHERNET_CONN_RETRY_DELAY_MS        (500)

/* Application ID */
#define APP_ID                              (0)

/* Ethernet interface ID */
#ifdef XMC7100D_F176K4160
#define INTERFACE_ID                        CY_ECM_INTERFACE_ETH0
#else
#define INTERFACE_ID                        CY_ECM_INTERFACE_ETH1
#endif

/*******************************************************************************
* Forward declaration
********************************************************************************/
cy_rslt_t ethernet_connect(void);
cy_ota_callback_results_t ota_callback(cy_ota_cb_struct_t *cb_data);
void print_heap_usage(char *msg);

/*******************************************************************************
* Global Variables
********************************************************************************/
/* OTA context */
cy_ota_context_ptr ota_context;

/* Network parameters for OTA */
cy_ota_network_params_t ota_network_params =
{
    .http =
    {
        .server =
        {
            .host_name = HTTP_SERVER,
            .port = HTTP_SERVER_PORT
        },
        .file = OTA_HTTP_JOB_FILE,
    #if (ENABLE_TLS == true)
        .credentials =
        {
            .root_ca = ROOT_CA_CERTIFICATE,
            .root_ca_size = sizeof(ROOT_CA_CERTIFICATE),
        #if (USING_CLIENT_CERTIFICATE == true)
            .client_cert = CLIENT_CERTIFICATE,
            .client_cert_size = sizeof(CLIENT_CERTIFICATE),
        #endif
        #if (USING_CLIENT_KEY == true)
            .private_key = CLIENT_KEY,
            .private_key_size = sizeof(CLIENT_KEY),
        #endif
        },
    #endif
    },
    .use_get_job_flow = CY_OTA_JOB_FLOW,
#if (ENABLE_TLS == true)
    .initial_connection = CY_OTA_CONNECTION_HTTPS,
#else
    .initial_connection = CY_OTA_CONNECTION_HTTP
#endif
};

/* Parameters for OTA agent */
cy_ota_agent_params_t ota_agent_params =
{
    .cb_func = ota_callback,
    .cb_arg = &ota_context,
    .reboot_upon_completion = 1, /* Reboot after completing OTA with success. */
    .validate_after_reboot = 1,
    .do_not_send_result = 1
};

/* OTA storage interface callbacks */
cy_ota_storage_interface_t ota_interfaces =
{
   .ota_file_open            = cy_ota_storage_open,
   .ota_file_read            = cy_ota_storage_read,
   .ota_file_write           = cy_ota_storage_write,
   .ota_file_close           = cy_ota_storage_close,
   .ota_file_verify          = cy_ota_storage_verify,
   .ota_file_validate        = cy_ota_storage_image_validate,
   .ota_file_get_app_info    = cy_ota_storage_get_app_info
};

/*******************************************************************************
 * Function Name: ota_task
 *******************************************************************************
 * Summary:
 *  Task to initialize required libraries and start OTA agent.
 *
 * Parameters:
 *  void *args : Task parameter defined during task creation (unused)
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void ota_task(void *args)
{
    /* initialize OTA storage */
    if (CY_RSLT_SUCCESS != cy_ota_storage_init())
    {
        printf("\n Initializing ota storage failed.\n");
        CY_ASSERT(0);
    }

#ifndef TEST_REVERT
    /* Validate the update so we do not revert */
    if(CY_RSLT_SUCCESS != cy_ota_storage_image_validate(APP_ID))
    {
        printf("\n Failed to validate the update.\n");
        CY_ASSERT(0);
    }
#endif

    /* Connect to Ethernet */
    if(CY_RSLT_SUCCESS != ethernet_connect())
    {
        printf("\n Failed to connect to Ethernet.\n");
        CY_ASSERT(0);
    }

    /* Initialize underlying support code that is needed for OTA and HTTP */
    if (CY_RSLT_SUCCESS != cy_awsport_network_init())
    {
        printf("\n Initializing secure sockets failed.\n");
        CY_ASSERT(0);
    }

    /* Initialize and start the OTA agent */
    if(CY_RSLT_SUCCESS != cy_ota_agent_start(&ota_network_params, &ota_agent_params, &ota_interfaces, &ota_context))
    {
        printf("\n Initializing and starting the OTA agent failed.\n");
        CY_ASSERT(0);
    }

    vTaskSuspend( NULL );
 }
cy_ecm_phy_callbacks_t phy_callbacks =
{
        .phy_init = cy_eth_phy_init,
        .phy_configure = cy_eth_phy_configure,
        .phy_enable_ext_reg = cy_eth_phy_enable_ext_reg,
        .phy_discover = cy_eth_phy_discover,
        .phy_get_auto_neg_status = cy_eth_phy_get_auto_neg_status,
        .phy_get_link_partner_cap = cy_eth_phy_get_link_partner_cap,
        .phy_get_linkspeed = cy_eth_phy_get_linkspeed,
        .phy_get_linkstatus = cy_eth_phy_get_linkstatus,
        .phy_reset = cy_eth_phy_reset
};
/******************************************************************************
 * Function Name: ethernet_connect
 ******************************************************************************
 * Summary:
 *  Function that initiates connection to the Ethernet. This function initializes
 *  the Ethernet interface and then tries to establish a connection with the network.
 *  it retries to connect with the network a maximum of 'MAX_CONNECTION_RETRIES' times.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  cy_rslt_t : CY_RSLT_SUCCESS upon a successful Ethernet connection, else an
 *              error code indicating the failure.
 *
 ******************************************************************************/
cy_rslt_t ethernet_connect(void)
{
    cy_rslt_t result = CY_RSLT_TYPE_ERROR;

    /* Structure to store IP address of the Ethernet connection. */
    cy_ecm_ip_address_t ip_addr;

    /* Pointer to store the Ethernet connection manager handle allocated
     * after the successful initialization of the Ethernet interface. */
    cy_ecm_t ecm_handle;

    /* Variables used by Ethernet connection manager.*/
    uint8_t conn_retries = 0;

    /* Initialize Ethernet connection manager. */
    result = cy_ecm_init();

    if (CY_RSLT_SUCCESS != result)
    {
        printf("\n Ethernet Connection Manager initialization failed\n");
        CY_ASSERT(0);
    }

    printf("Initiating cy_ecm_ethif_init \n");
    result =  cy_ecm_ethif_init(INTERFACE_ID, &phy_callbacks, &ecm_handle);

    if(CY_RSLT_SUCCESS != result)
    {
        printf("\nEthernet Interface initialization failed!\n");
        CY_ASSERT(0);
    }

    /* Connect to Ethernet */
    for(conn_retries = 0; conn_retries < MAX_CONNECTION_RETRIES; conn_retries++)
    {
        printf("Initiating cy_ecm_connect \n");
        result = cy_ecm_connect(ecm_handle, NULL, &ip_addr);

        if (CY_RSLT_SUCCESS == result)
        {
            printf("Successfully connected to Ethernet.\n");
            printf("IP Address Assigned: %d.%d.%d.%d\n", (uint8)ip_addr.ip.v4, (uint8)(ip_addr.ip.v4 >> 8),
                    (uint8)(ip_addr.ip.v4 >> 16), (uint8)(ip_addr.ip.v4 >> 24));
            return result;
        }

        printf( "Connection to Ethernet network failed with error code %d."
                "Retrying in %d ms...\n", (int) result, ETHERNET_CONN_RETRY_DELAY_MS );
        vTaskDelay(pdMS_TO_TICKS(ETHERNET_CONN_RETRY_DELAY_MS));
    }

    printf( "Exceeded maximum Ethernet connection attempts\n" );

    return result;
}

/*******************************************************************************
 * Function Name: ota_callback()
 *******************************************************************************
 * Summary:
 *  Prints the status of the OTA agent on every event. This callback is optional,
 *  but be aware that the OTA middleware will not print the status of OTA agent
 *  on its own.
 *
 * Return:
 *  CY_OTA_CB_RSLT_OTA_CONTINUE - OTA Agent to continue with function.
 *  CY_OTA_CB_RSLT_OTA_STOP     - OTA Agent to End current update session.
 *  CY_OTA_CB_RSLT_APP_SUCCESS  - Application completed task, success.
 *  CY_OTA_CB_RSLT_APP_FAILED   - Application completed task, failure.
 *
 *******************************************************************************/
cy_ota_callback_results_t ota_callback(cy_ota_cb_struct_t *cb_data)
{
    cy_ota_callback_results_t   cb_result = CY_OTA_CB_RSLT_OTA_CONTINUE;
    const char                  *state_string;
    const char                  *error_string;

    if (cb_data == NULL)
    {
        return CY_OTA_CB_RSLT_OTA_STOP;
    }

    state_string  = cy_ota_get_state_string(cb_data->ota_agt_state);
    error_string  = cy_ota_get_error_string(cy_ota_get_last_error());

    print_heap_usage("In OTA Callback");

    switch (cb_data->reason)
    {

        case CY_OTA_LAST_REASON:
            break;

        case CY_OTA_REASON_SUCCESS:
            printf(">> APP CB OTA SUCCESS state:%d %s last_error:%s\n\n",
                    cb_data->ota_agt_state,
                    state_string, error_string);
            break;

        case CY_OTA_REASON_FAILURE:
            printf(">> APP CB OTA FAILURE state:%d %s last_error:%s\n\n",
                    cb_data->ota_agt_state, state_string, error_string);
            break;

        case CY_OTA_REASON_STATE_CHANGE:
            switch (cb_data->ota_agt_state)
            {
                case CY_OTA_STATE_NOT_INITIALIZED:
                case CY_OTA_STATE_EXITING:
                case CY_OTA_STATE_INITIALIZING:
                case CY_OTA_STATE_AGENT_STARTED:
                case CY_OTA_STATE_AGENT_WAITING:
                    break;

                case CY_OTA_STATE_START_UPDATE:
                    printf("APP CB OTA STATE CHANGE CY_OTA_STATE_START_UPDATE\n");
                    break;

                case CY_OTA_STATE_JOB_CONNECT:
                    printf("APP CB OTA CONNECT FOR JOB using ");
                    /* NOTE:
                     *  HTTP - json_doc holds the HTTP "GET" request
                     */
                    if ((cb_data->broker_server.host_name == NULL)  ||
                        ( cb_data->broker_server.port == 0)         ||
                        ( strlen(cb_data->file) == 0) )
                    {
                        printf("ERROR in callback data: HTTP: server: %p port: %d topic: '%p'\n",
                                cb_data->broker_server.host_name,
                                cb_data->broker_server.port,
                                cb_data->file);
                        cb_result = CY_OTA_CB_RSLT_OTA_STOP;
                    }
                    printf("HTTP: server:%s port: %d file: '%s'\n",
                            cb_data->broker_server.host_name,
                            cb_data->broker_server.port,
                            cb_data->file);

                    break;

                case CY_OTA_STATE_JOB_DOWNLOAD:
                    printf("APP CB OTA JOB DOWNLOAD using ");
                    /* NOTE:
                     *  HTTP - json_doc holds the HTTP "GET" request
                     */
                    printf("HTTP: '%s'\n", cb_data->file);
                    break;

                case CY_OTA_STATE_JOB_DISCONNECT:
                    printf("APP CB OTA JOB DISCONNECT\n");
                    break;

                case CY_OTA_STATE_JOB_PARSE:
                    printf("APP CB OTA PARSE JOB: '%.*s' \n",
                    strlen(cb_data->json_doc),
                    cb_data->json_doc);
                    break;

                case CY_OTA_STATE_JOB_REDIRECT:
                    printf("APP CB OTA JOB REDIRECT\n");
                    break;

                case CY_OTA_STATE_DATA_CONNECT:
                    printf("APP CB OTA CONNECT FOR DATA using ");
                    printf("HTTP: %s:%d \n", cb_data->broker_server.host_name,
                    cb_data->broker_server.port);
                    break;

                case CY_OTA_STATE_DATA_DOWNLOAD:
                    printf("APP CB OTA DATA DOWNLOAD using ");
                    /* NOTE:
                     *  HTTP - json_doc holds the HTTP "GET" request
                     */
                    printf("HTTP: '%.*s' ", strlen(cb_data->json_doc), cb_data->json_doc);
                    printf("File: '%s'\n\n", cb_data->file);
                    break;

                case CY_OTA_STATE_DATA_DISCONNECT:
                    printf("APP CB OTA DATA DISCONNECT\n");
                    break;

                case CY_OTA_STATE_RESULT_CONNECT:
                    printf("APP CB OTA SEND RESULT CONNECT using ");
                    /* NOTE:
                     *  HTTP - json_doc holds the HTTP "GET" request
                     */
                    printf("HTTP: Server:%s port: %d\n",
                            cb_data->broker_server.host_name,
                            cb_data->broker_server.port);
                    break;

                case CY_OTA_STATE_RESULT_SEND:
                    printf("APP CB OTA SENDING RESULT using ");
                    /* NOTE:
                     *  HTTP - json_doc holds the HTTP "PUT"
                     */
                    printf("HTTP: '%s' \n", cb_data->json_doc);
                    break;

                case CY_OTA_STATE_RESULT_RESPONSE:
                    printf("APP CB OTA Got Result response\n");
                    break;

                case CY_OTA_STATE_RESULT_DISCONNECT:
                    printf("APP CB OTA Result Disconnect\n");
                    break;

                case CY_OTA_STATE_OTA_COMPLETE:
                    printf("APP CB OTA Session Complete\n");
                    break;

                case CY_OTA_STATE_STORAGE_OPEN:
                    printf("APP CB OTA STORAGE OPEN\n");
                    break;

                case CY_OTA_STATE_STORAGE_WRITE:
                    printf("APP CB OTA STORAGE WRITE %ld%% (%ld of %ld)\n",
                            (unsigned long)cb_data->percentage,
                            (unsigned long)cb_data->bytes_written,
                            (unsigned long)cb_data->total_size);

                    /* Move cursor to previous line */
                    printf("\x1b[1F");
                    break;

                case CY_OTA_STATE_STORAGE_CLOSE:
                    printf("APP CB OTA STORAGE CLOSE\n");
                    break;

                case CY_OTA_STATE_VERIFY:
                    printf("APP CB OTA VERIFY\n");
                    break;

                case CY_OTA_STATE_RESULT_REDIRECT:
                    printf("APP CB OTA RESULT REDIRECT\n");
                    break;

                case CY_OTA_NUM_STATES:
                    break;
            }   /* switch state */
            break;
    }

    return cb_result;
}
