/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include <logging/log.h>
#include "fru.h"
#include "plat_fru.h"
#include "libutil.h"

LOG_MODULE_REGISTER(plat_fru);

const EEPROM_CFG plat_fru_config[] = {};

void pal_load_fru_config(void)
{
	memcpy(fru_config, plat_fru_config, sizeof(plat_fru_config));
}