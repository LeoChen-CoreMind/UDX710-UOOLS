/**
 * API调用封装
 * 统一管理所有后端接口调用
 */

const BASE_URL = ''

// 获取存储的Token
function getAuthToken() {
  return localStorage.getItem('auth_token') || ''
}

// 清除Token（登出时使用）
export function clearAuthToken() {
  localStorage.removeItem('auth_token')
}

// 设置Token
export function setAuthToken(token) {
  localStorage.setItem('auth_token', token)
}

// 检查是否已登录
export function isLoggedIn() {
  return !!getAuthToken()
}

// 导出带认证的fetch函数（供组件直接使用）
export async function authFetch(url, options = {}) {
  const token = getAuthToken()
  const headers = {
    ...options.headers
  }

  if (token) {
    headers['Authorization'] = `Bearer ${token}`
  }

  const response = await fetch(url, {
    ...options,
    headers
  })

  // 处理401未授权响应
  if (response.status === 401) {
    clearAuthToken()
    window.dispatchEvent(new CustomEvent('auth-required'))
  }

  return response
}

// 通用请求函数
async function request(url, options = {}) {
  const token = getAuthToken()
  const headers = {
    'Content-Type': 'application/json',
    ...options.headers
  }

  // 添加Authorization头
  if (token) {
    headers['Authorization'] = `Bearer ${token}`
  }

  const response = await fetch(`${BASE_URL}${url}`, {
    headers,
    ...options
  })

  // 处理401未授权响应
  if (response.status === 401) {
    clearAuthToken()
    // 触发自定义事件通知App.vue
    window.dispatchEvent(new CustomEvent('auth-required'))
    throw new Error('未授权，请重新登录')
  }

  if (!response.ok) {
    throw new Error(`HTTP错误: ${response.status}`)
  }
  return response.json()
}

// ==================== 系统信息API ====================

// 获取系统信息
export async function fetchSystemInfo() {
  return request('/api/info')
}

// 清除系统缓存
export async function clearCache() {
  return request('/api/clear_cache', { method: 'POST' })
}

// 设备控制（重启/关机）
export async function deviceControl(action) {
  return request('/api/device_control', {
    method: 'POST',
    body: JSON.stringify({ action })
  })
}

// 获取设备序列号
export async function getDeviceSerial() {
  return request('/api/serial')
}

// 提交激活码
export async function submitActivationKey(key) {
  return request('/api/key', {
    method: 'POST',
    body: JSON.stringify({ key })
  })
}

// ==================== 网络管理API ====================

// 切换飞行模式
export async function toggleAirplaneMode(enabled) {
  return request('/api/airplane_mode', {
    method: 'POST',
    body: JSON.stringify({ enabled })
  })
}

// 设置网络模式
export async function setNetworkMode(mode) {
  return request('/api/set_network', {
    method: 'POST',
    body: JSON.stringify({ mode })
  })
}

// 切换SIM卡槽
export async function switchSlot(slot) {
  return request('/api/switch', {
    method: 'POST',
    body: JSON.stringify({ slot })
  })
}

// ==================== 流量统计API ====================

// 获取流量统计
export async function getTrafficTotal() {
  return request('/api/get/Total')
}

// 获取流量配置
export async function getTrafficConfig() {
  return request('/api/get/set')
}

// 设置流量限制
export async function setTrafficLimit(enabled, limitGB) {
  const switchValue = enabled ? 1 : 0
  const muchValue = Math.round(limitGB * 1073741824) // GB转字节
  return request('/api/set/total', {
    method: 'POST',
    body: JSON.stringify({ switch: switchValue, much: muchValue })
  })
}

// 清除流量统计
export async function clearTrafficStats() {
  return request('/api/set/total', {
    method: 'POST',
    body: JSON.stringify({})
  })
}

// ==================== 定时重启API ====================

// 获取定时重启配置
export async function getRebootConfig() {
  return request('/api/get/first-reboot')
}

// 设置定时重启
export async function setReboot(days, hour, minute) {
  const dayStr = days.join(',')
  return request(`/api/set/reboot?day=${dayStr}&hour=${hour}&minute=${minute}`)
}

// 清除定时重启
export async function clearReboot() {
  return request('/api/claen/cron')
}

// ==================== 系统时间API ====================

// 获取系统时间
export async function getSystemTime() {
  return request('/api/get/time')
}

// NTP同步系统时间
export async function syncSystemTime() {
  return request('/api/set/time', { method: 'POST' })
}

// ==================== 数据连接和漫游API ====================

// 获取数据连接状态
export async function getDataStatus() {
  return request('/api/data')
}

// 设置数据连接状态
export async function setDataStatus(active) {
  return request('/api/data', {
    method: 'POST',
    body: JSON.stringify({ active })
  })
}

// 获取漫游状态
export async function getRoamingStatus() {
  return request('/api/roaming')
}

// 设置漫游允许状态
export async function setRoamingAllowed(allowed) {
  return request('/api/roaming', {
    method: 'POST',
    body: JSON.stringify({ allowed })
  })
}

// ==================== 高级网络API ====================

// 获取频段状态
export async function getBands() {
  return request('/api/bands')
}

// 获取当前连接的频段
export async function getCurrentBand() {
  return request('/api/current_band')
}

// 锁定频段
export async function lockBands(bands) {
  return request('/api/lock_bands', {
    method: 'POST',
    body: JSON.stringify({ bands })
  })
}

// 解锁所有频段
export async function unlockBands() {
  return request('/api/unlock_bands', { method: 'POST' })
}

// 获取小区信息
export async function getCells() {
  return request('/api/cells')
}

// 锁定小区
export async function lockCell(technology, arfcn, pci) {
  return request('/api/lock_cell', {
    method: 'POST',
    body: JSON.stringify({
      technology,
      arfcn: arfcn.toString(),
      pci: pci.toString()
    })
  })
}

// 解锁小区
export async function unlockCell() {
  return request('/api/unlock_cell', { method: 'POST' })
}


// ==================== 充电控制API ====================

// 获取充电配置和电池状态
export async function getChargeConfig() {
  return request('/api/charge/config')
}

// 设置充电配置
export async function setChargeConfig(enabled, startThreshold, stopThreshold) {
  return request('/api/charge/config', {
    method: 'POST',
    body: JSON.stringify({ enabled, startThreshold, stopThreshold })
  })
}

// 手动开启充电
export async function chargeOn() {
  return request('/api/charge/on', { method: 'POST' })
}

// 手动停止充电
export async function chargeOff() {
  return request('/api/charge/off', { method: 'POST' })
}


// ==================== AT命令调试API ====================

// 执行AT命令
export async function executeAT(command) {
  return request('/api/at', {
    method: 'POST',
    body: JSON.stringify({ command })
  })
}

// ==================== USB模式切换API ====================

// 获取当前USB模式
export async function getUsbMode() {
  return request('/api/usb/mode')
}

// 设置USB模式
export async function setUsbMode(mode, permanent = false) {
  return request('/api/usb/mode', {
    method: 'POST',
    body: JSON.stringify({ mode, permanent })
  })
}

// USB热切换（立即生效）
export async function usbAdvanceSwitch(mode) {
  return request('/api/usb-advance', {
    method: 'POST',
    body: JSON.stringify({ mode })
  })
}

// ==================== APN配置API ====================

// 获取APN列表
export async function getApnList() {
  return request('/api/apn')
}

// 设置APN配置
export async function setApnConfig(config) {
  return request('/api/apn', {
    method: 'POST',
    body: JSON.stringify(config)
  })
}

// ==================== 通用API封装 ====================

// 返回一个包含get/post方法的对象，用于组件中调用
export function useApi() {
  return {
    async get(url) {
      try {
        const data = await request(url)
        return { ok: true, data }
      } catch (err) {
        return { ok: false, data: { error: err.message } }
      }
    },
    async post(url, data = {}) {
      try {
        const result = await request(url, {
          method: 'POST',
          body: JSON.stringify(data)
        })
        return { ok: true, data: result }
      } catch (err) {
        return { ok: false, data: { error: err.message } }
      }
    }
  }
}


// ==================== 插件管理API ====================

// 执行Shell命令
export async function executeShell(command) {
  return request('/api/shell', {
    method: 'POST',
    body: JSON.stringify({ command })
  })
}

// 获取插件列表
export async function getPluginList() {
  return request('/api/plugins')
}

// 上传插件
export async function uploadPlugin(name, content) {
  return request('/api/plugins', {
    method: 'POST',
    body: JSON.stringify({ name, content })
  })
}

// 删除指定插件
export async function deletePlugin(name) {
  return request(`/api/plugins/${encodeURIComponent(name)}`, {
    method: 'DELETE'
  })
}

// 删除所有插件
export async function deleteAllPlugins() {
  return request('/api/plugins/all', {
    method: 'DELETE'
  })
}

// ==================== 插件存储API ====================

// 获取插件存储数据
export async function getPluginStorage(pluginName) {
  return request(`/api/plugins/storage/${encodeURIComponent(pluginName)}`)
}

// 设置插件存储数据
export async function setPluginStorage(pluginName, data) {
  return request(`/api/plugins/storage/${encodeURIComponent(pluginName)}`, {
    method: 'POST',
    body: JSON.stringify(data)
  })
}

// 删除插件存储数据
export async function deletePluginStorage(pluginName) {
  return request(`/api/plugins/storage/${encodeURIComponent(pluginName)}`, {
    method: 'DELETE'
  })
}


// ==================== 脚本管理API ====================

// 获取脚本列表
export async function getScriptList() {
  return request('/api/scripts')
}

// 上传脚本
export async function uploadScript(name, content) {
  return request('/api/scripts', {
    method: 'POST',
    body: JSON.stringify({ name, content })
  })
}

// 更新脚本
export async function updateScript(name, content) {
  return request(`/api/scripts/${encodeURIComponent(name)}`, {
    method: 'PUT',
    body: JSON.stringify({ content })
  })
}

// 删除脚本
export async function deleteScript(name) {
  return request(`/api/scripts/${encodeURIComponent(name)}`, {
    method: 'DELETE'
  })
}


// ==================== 认证API ====================

// 登录
export async function authLogin(password) {
  const response = await fetch('/api/auth/login', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ password })
  })
  const data = await response.json()
  if (response.ok && data.token) {
    localStorage.setItem('auth_token', data.token)
  }
  return { ok: response.ok, data }
}

// 登出
export async function authLogout() {
  try {
    await request('/api/auth/logout', { method: 'POST' })
  } finally {
    clearAuthToken()
  }
}

// 修改密码
export async function authChangePassword(oldPassword, newPassword) {
  return request('/api/auth/password', {
    method: 'POST',
    body: JSON.stringify({ old_password: oldPassword, new_password: newPassword })
  })
}

// 获取登录状态
export async function authGetStatus() {
  const token = getAuthToken()
  const headers = token ? { 'Authorization': `Bearer ${token}` } : {}

  const response = await fetch('/api/auth/status', { headers })
  return response.json()
}

// ==================== 网络接口监控API ====================

// 获取所有网络接口列表
export async function getNetifList() {
  return request('/api/netif/list')
}

// 获取指定接口实时流量统计
export async function getNetifStats(ifname) {
  return request('/api/netif/stats', {
    method: 'POST',
    body: JSON.stringify({ interface: ifname })
  })
}

// 设置接口监听状态
export async function setNetifMonitor(ifname, enabled) {
  return request('/api/netif/monitor', {
    method: 'POST',
    body: JSON.stringify({ interface: ifname, enabled })
  })
}

// ==================== 密保API ====================

// 获取密保状态
export async function getSecurityStatus() {
  return request('/api/security/status')
}

// 设置密保问题
export async function setupSecurityQuestions(q1, a1, q2, a2) {
  return request('/api/security/setup', {
    method: 'POST',
    body: JSON.stringify({ question1: q1, answer1: a1, question2: q2, answer2: a2 })
  })
}

// 获取密保问题（用于验证）
export async function getSecurityQuestions() {
  return request('/api/security/questions')
}

// 重置密码（需验证密保）
export async function securityResetPassword(a1, a2, confirmation) {
  return request('/api/security/reset-password', {
    method: 'POST',
    body: JSON.stringify({ answer1: a1, answer2: a2, confirm: confirmation })
  })
}

// 恢复出厂设置（需验证密保）
export async function securityFactoryReset(a1, a2, confirmation) {
  return request('/api/security/factory-reset', {
    method: 'POST',
    body: JSON.stringify({ answer1: a1, answer2: a2, confirm: confirmation })
  })
}
