/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef JWT_IMPL_H__
#define JWT_IMPL_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generate a JWT (JSON Web Token) for nRF Cloud authentication
 *
 * This function creates a JWT token signed with the device's private key
 * for authentication with nRF Cloud services.
 *
 * @param time_valid_s Time in seconds for which the JWT should be valid.
 *                     If 0, uses default validity period.
 *                     If > NRF_CLOUD_JWT_VALID_TIME_S_MAX, clamps to maximum.
 * @param jwt_buf      Buffer to store the generated JWT string.
 * @param jwt_buf_sz   Size of the JWT buffer.
 *
 * @retval 0        JWT successfully generated.
 * @retval -EINVAL  Invalid parameters.
 * @retval -ENOMEM  JWT buffer too small.
 * @retval -EBADF   Private key format error.
 * @retval <0       Other error codes from underlying subsystems.
 */
int nrf_cloud_jwt_generate(uint32_t time_valid_s, char *const jwt_buf, size_t jwt_buf_sz);

#ifdef __cplusplus
}
#endif

#endif /* JWT_IMPL_H__ */
