/*
   OpenChange Server implementation

   EMSMDBP: EMSMDB Provider implementation

   Copyright (C) Julien Kerihuel 2009

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
   \file oxcprpt.c

   \brief Property and Stream Object routines and Rops
 */

#include "mapiproxy/dcesrv_mapiproxy.h"
#include "mapiproxy/libmapiproxy/libmapiproxy.h"
#include "mapiproxy/libmapiserver/libmapiserver.h"
#include "dcesrv_exchange_emsmdb.h"


/**
   \details Retrieve properties on a mailbox object.

   \param mem_ctx pointer to the memory object
   \param emsmdbp_ctx pointer to the emsmdb provider context
   \param request GetProps request
   \param response GetProps reply
   \param private_data pointer to the private data stored for this
   object

   \note Mailbox objects have a limited set of supported properties.

   \return MAPI_E_SUCCESS on success, otherwise MAPI error
 */
static enum MAPISTATUS RopGetPropertiesSpecific_Mailbox(TALLOC_CTX *mem_ctx,
							struct emsmdbp_context *emsmdbp_ctx,
							struct GetProps_req request,
							struct GetProps_repl *response,
							void *private_data)
{
	enum MAPISTATUS			retval;
	struct emsmdbp_object_mailbox	*object;
	struct SBinary_short		bin;
	uint32_t			i;
	uint32_t			error = 0;

	/* Sanity checks */
	OPENCHANGE_RETVAL_IF(!private_data, MAPI_E_INVALID_PARAMETER, NULL);

	object = (struct emsmdbp_object_mailbox *) private_data;

	/* Step 1. Check if we need a layout */
	response->layout = 0;
	for (i = 0; i < request.prop_count; i++) {
		switch (request.properties[i]) {
		case PR_MAPPING_SIGNATURE:
		case PR_IPM_PUBLIC_FOLDERS_ENTRYID:
			response->layout = 0x1;
			break;
		default:
			break;
		}
	}

	/* Step 2. Fill the GetProps blob */
	response->prop_data.length = 0;
	response->prop_data.data = NULL;

	for (i = 0; i < request.prop_count; i++) {
		switch (request.properties[i]) {
		case PR_MAPPING_SIGNATURE:
		case PR_IPM_PUBLIC_FOLDERS_ENTRYID:
			error = MAPI_E_NO_ACCESS;
			request.properties[i] = (request.properties[i] & 0xFFFF0000) + PT_ERROR;
			libmapiserver_push_property(mem_ctx, lp_iconv_convenience(emsmdbp_ctx->lp_ctx),
						    request.properties[i], (const void *)&error, 
						    &response->prop_data, response->layout);
			break;
		case PR_USER_ENTRYID:
			retval = entryid_set_AB_EntryID(mem_ctx, object->szUserDN, &bin);
			libmapiserver_push_property(mem_ctx, lp_iconv_convenience(emsmdbp_ctx->lp_ctx),
						    request.properties[i], (const void *)&bin,
						    &response->prop_data, response->layout);
			talloc_free(bin.lpb);
			break;
		case PR_MAILBOX_OWNER_ENTRYID:
			retval = entryid_set_AB_EntryID(mem_ctx, object->owner_EssDN, &bin);
			libmapiserver_push_property(mem_ctx, lp_iconv_convenience(emsmdbp_ctx->lp_ctx),
						    request.properties[i], (const void *)&bin,
						    &response->prop_data, response->layout);
			talloc_free(bin.lpb);
			break;
		case PR_MAILBOX_OWNER_NAME:
		case PR_MAILBOX_OWNER_NAME_UNICODE:
			libmapiserver_push_property(mem_ctx, lp_iconv_convenience(emsmdbp_ctx->lp_ctx),
						    request.properties[i], (const void *)object->owner_Name,
						    &response->prop_data, response->layout);
			break;
		default:
			error = MAPI_E_NOT_FOUND;
			request.properties[i] = (request.properties[i] & 0xFFFF0000) + PT_ERROR;
			libmapiserver_push_property(mem_ctx, lp_iconv_convenience(emsmdbp_ctx->lp_ctx),
						    request.properties[i], (const void *)&error, 
						    &response->prop_data, response->layout);
			break;
		}
	}

	return MAPI_E_SUCCESS;
}


/**
   \details EcDoRpc GetPropertiesSpecific (0x07) Rop. This operation
   retrieves from properties data from specified object.

   \param mem_ctx pointer to the memory context
   \param emsmdbp_ctx pointer to the emsmdb provider context
   \param mapi_req pointer to the GetPropertiesSpecific
   EcDoRpc_MAPI_REQ structure
   \param mapi_repl pointer to the GetPropertiesSpecific
   EcDoRpc_MAPI_REPL structure
   \param handles pointer to the MAPI handles array
   \param size pointer to the mapi_response size to update

   \return MAPI_E_SUCCESS on success, otherwise MAPI error
 */
_PUBLIC_ enum MAPISTATUS EcDoRpc_RopGetPropertiesSpecific(TALLOC_CTX *mem_ctx,
							  struct emsmdbp_context *emsmdbp_ctx,
							  struct EcDoRpc_MAPI_REQ *mapi_req,
							  struct EcDoRpc_MAPI_REPL *mapi_repl,
							  uint32_t *handles, uint16_t *size)
{
	enum MAPISTATUS		retval;
	struct GetProps_req	request;
	struct GetProps_repl	response;
	uint32_t		handle;
	struct mapi_handles	*rec = NULL;
	int			systemfolder = -1;
	void			*private_data = NULL;

	DEBUG(4, ("exchange_emsmdb: [OXCPRPT] GetPropertiesSpecific (0x07)\n"));

	/* Sanity checks */
	OPENCHANGE_RETVAL_IF(!emsmdbp_ctx, MAPI_E_NOT_INITIALIZED, NULL);
	OPENCHANGE_RETVAL_IF(!mapi_req, MAPI_E_INVALID_PARAMETER, NULL);
	OPENCHANGE_RETVAL_IF(!mapi_repl, MAPI_E_INVALID_PARAMETER, NULL);
	OPENCHANGE_RETVAL_IF(!handles, MAPI_E_INVALID_PARAMETER, NULL);
	OPENCHANGE_RETVAL_IF(!size, MAPI_E_INVALID_PARAMETER, NULL);

	request = mapi_req->u.mapi_GetProps;
	response = mapi_repl->u.mapi_GetProps;

	handle = handles[mapi_req->handle_idx];
	retval = mapi_handles_search(emsmdbp_ctx->handles_ctx, handle, &rec);
	OPENCHANGE_RETVAL_IF(retval, retval, NULL);

	retval = mapi_handles_get_systemfolder(rec, &systemfolder);
	retval = mapi_handles_get_private_data(rec, &private_data);

	DEBUG(0, ("GetProps: SystemFolder = %d\n", systemfolder));

	switch (systemfolder) {
	case -1:
		/* handled by mapistore */
		break;
	case 0x0:
		/* private mailbox */
		retval = RopGetPropertiesSpecific_Mailbox(mem_ctx, emsmdbp_ctx, request, &response, private_data);
		break;
	default:
		/* system folder */
		break;
	}

	/* Fill EcDoRpc_MAPI_REPL reply */
	mapi_repl->opnum = mapi_req->opnum;
	mapi_repl->handle_idx = mapi_req->handle_idx;
	mapi_repl->error_code = MAPI_E_SUCCESS;
	mapi_repl->u.mapi_GetProps = response;

	*size = libmapiserver_RopGetPropertiesSpecific_size(mapi_req, mapi_repl);

	return MAPI_E_SUCCESS;
}