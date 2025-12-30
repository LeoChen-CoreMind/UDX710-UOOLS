<script setup>
import { ref, onMounted, computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { getBands, lockBands as apiLockBands, unlockBands as apiUnlockBands, getCells, lockCell as apiLockCell, unlockCell as apiUnlockCell } from '../composables/useApi'
import { useToast } from '../composables/useToast'
import { useConfirm } from '../composables/useConfirm'

const { t } = useI18n()
const { success, error: showError } = useToast()
const { confirm } = useConfirm()

const bands = ref({ '4G_TDD': [], '4G_FDD': [], '5G': [] })
const cells = ref([])
const loading = ref(false)
const lockingBands = ref(false)
const lockingCell = ref(false)
const refreshing = ref(false)

// 自定义锁定相关状态
const showCustomLock = ref(false)
const customLocking = ref(false)
const customLockForm = ref({
  rat: 'LTE',
  arfcn: '',
  pci: ''
})

const servingCell = computed(() => cells.value.find(c => c.isServing))
const neighborCells = computed(() => cells.value.filter(c => !c.isServing))

// 计算小区信号综合评分（用于推荐最佳小区）
function calculateCellScore(cell) {
  // RSRP: -140 ~ -44 dBm, 越大越好
  // RSRQ: -20 ~ -3 dB, 越大越好  
  // SINR: -20 ~ 30 dB, 越大越好
  const rsrp = cell.rsrp || -140
  const rsrq = cell.rsrq || -20
  const sinr = cell.sinr || -20
  
  // 归一化到 0-100
  const rsrpNorm = Math.max(0, Math.min(100, (rsrp + 140) / 96 * 100))
  const rsrqNorm = Math.max(0, Math.min(100, (rsrq + 20) / 17 * 100))
  const sinrNorm = Math.max(0, Math.min(100, (sinr + 20) / 50 * 100))
  
  // 加权评分: RSRP 50%, RSRQ 25%, SINR 25%
  return rsrpNorm * 0.5 + rsrqNorm * 0.25 + sinrNorm * 0.25
}

// 推荐的最佳邻区
const recommendedCell = computed(() => {
  if (neighborCells.value.length === 0) return null
  let best = null
  let bestScore = -Infinity
  for (const cell of neighborCells.value) {
    const score = calculateCellScore(cell)
    if (score > bestScore) {
      bestScore = score
      best = cell
    }
  }
  return best
})

// 判断是否为推荐小区
function isRecommended(cell) {
  if (!recommendedCell.value) return false
  return cell.pci === recommendedCell.value.pci && 
         cell.arfcn === recommendedCell.value.arfcn &&
         cell.rat === recommendedCell.value.rat
}

const selectedBandsCount = computed(() => {
  let count = 0
  Object.values(bands.value).forEach(group => group.forEach(band => { if (band.locked) count++ }))
  return count
})

async function fetchBands() {
  loading.value = true
  try { bands.value = await getBands() }
  catch (err) { showError(t('advanced.getBandsFailed') + ': ' + err.message) }
  finally { loading.value = false }
}

async function handleLockBands() {
  const selectedBands = []
  Object.values(bands.value).forEach(group => group.forEach(band => { if (band.locked) selectedBands.push(band.name) }))
  if (selectedBands.length === 0) { showError(t('advanced.selectAtLeastOne')); return }
  lockingBands.value = true
  try { await apiLockBands(selectedBands); success(t('advanced.lockedBands', { count: selectedBands.length })); await fetchBands() }
  catch (err) { showError(t('advanced.lockFailed') + ': ' + err.message) }
  finally { lockingBands.value = false }
}

async function handleUnlockAllBands() {
  if (!await confirm({ title: t('advanced.unlockBand'), message: t('advanced.confirmUnlockAll') })) return
  lockingBands.value = true
  try { await apiUnlockBands(); Object.values(bands.value).forEach(g => g.forEach(b => b.locked = false)); success(t('advanced.unlockedAll')); await fetchBands() }
  catch (err) { showError(t('advanced.unlockFailed') + ': ' + err.message) }
  finally { lockingBands.value = false }
}

async function fetchCells() {
  refreshing.value = true
  try { const res = await getCells(); cells.value = res.Code === 0 && res.Data ? res.Data : [] }
  catch (err) { showError(t('advanced.getCellsFailed') + ': ' + err.message) }
  finally { refreshing.value = false }
}

async function handleLockCell(cell) {
  if (!await confirm({ title: t('advanced.lockCell'), message: t('advanced.confirmLockCell', { pci: cell.pci }) })) return
  lockingCell.value = true
  try { await apiLockCell(cell.rat, cell.arfcn, cell.pci); success(t('advanced.cellLockSuccess')); await fetchCells() }
  catch (err) { showError(t('advanced.lockFailed') + ': ' + err.message) }
  finally { lockingCell.value = false }
}

async function handleUnlockCell() {
  if (!await confirm({ title: t('advanced.unlockCell'), message: t('advanced.confirmUnlockCell') })) return
  lockingCell.value = true
  try { await apiUnlockCell(); success(t('advanced.cellUnlockSuccess')); await fetchCells() }
  catch (err) { showError(t('advanced.unlockFailed') + ': ' + err.message) }
  finally { lockingCell.value = false }
}

// 自定义锁定小区
async function handleCustomLockCell() {
  const { rat, arfcn, pci } = customLockForm.value
  if (!arfcn.trim() || !pci.trim()) {
    showError(t('advanced.enterArfcnPci'))
    return
  }
  if (!await confirm({ title: t('advanced.customLock'), message: t('advanced.confirmCustomLock', { rat, arfcn, pci }) })) return
  customLocking.value = true
  try {
    await apiLockCell(rat, arfcn.trim(), pci.trim())
    success(t('advanced.cellLockSuccess'))
    customLockForm.value = { rat: 'LTE', arfcn: '', pci: '' }
    showCustomLock.value = false
    await fetchCells()
  } catch (err) {
    showError(t('advanced.lockFailed') + ': ' + err.message)
  } finally {
    customLocking.value = false
  }
}

function getSignalColor(rsrp) {
  if (rsrp >= -80) return 'text-green-500'
  if (rsrp >= -90) return 'text-yellow-500'
  if (rsrp >= -100) return 'text-orange-500'
  return 'text-red-500'
}

onMounted(() => { fetchBands(); fetchCells() })
</script>

<template>
  <div class="space-y-4">
    <!-- 频段锁定 -->
    <div class="rounded-2xl bg-white/95 dark:bg-white/5 backdrop-blur border border-slate-200/60 dark:border-white/10 p-4 shadow-lg shadow-slate-200/40 dark:shadow-black/20 hover:shadow-xl hover:shadow-slate-300/50 dark:hover:shadow-black/30 transition-all duration-300">
      <!-- 标题栏 -->
      <div class="flex items-center justify-between mb-4">
        <div class="flex items-center space-x-3">
          <div class="w-10 h-10 rounded-xl bg-gradient-to-br from-purple-500 to-pink-500 flex items-center justify-center shadow-lg shadow-purple-500/30">
            <i class="fas fa-broadcast-tower text-white"></i>
          </div>
          <div>
            <h3 class="text-slate-900 dark:text-white font-semibold text-sm">{{ t('advanced.bandLock') }}</h3>
            <p class="text-slate-500 dark:text-white/50 text-xs">{{ t('advanced.selectedBands', { count: selectedBandsCount }) }}</p>
          </div>
        </div>
        <div class="flex items-center gap-2">
          <button @click="handleUnlockAllBands" :disabled="lockingBands"
            class="px-3 py-1.5 bg-slate-100 dark:bg-white/10 text-slate-600 dark:text-white/70 text-xs rounded-lg hover:bg-slate-200 dark:hover:bg-white/20 transition-all disabled:opacity-50">
            <i class="fas fa-unlock mr-1"></i>{{ t('advanced.unlock') }}
          </button>
          <button @click="handleLockBands" :disabled="lockingBands || selectedBandsCount === 0"
            class="px-3 py-1.5 bg-gradient-to-r from-purple-500 to-pink-500 text-white text-xs rounded-lg hover:shadow-lg hover:shadow-purple-500/30 transition-all disabled:opacity-50">
            <i :class="lockingBands ? 'fas fa-spinner animate-spin' : 'fas fa-lock'" class="mr-1"></i>
            {{ lockingBands ? t('advanced.locking') : t('advanced.lock') }}
          </button>
        </div>
      </div>

      <!-- 4G TDD -->
      <div class="mb-4">
        <div class="flex items-center space-x-2 mb-2">
          <div class="w-2 h-2 rounded-full bg-blue-500"></div>
          <span class="text-slate-700 dark:text-white/80 text-xs font-medium">4G TDD</span>
          <div class="flex-1 h-px bg-slate-200 dark:bg-white/10"></div>
          <span class="text-slate-400 dark:text-white/40 text-xs">{{ bands['4G_TDD'].filter(b => b.locked).length }}/{{ bands['4G_TDD'].length }}</span>
        </div>
        <div class="grid grid-cols-4 sm:grid-cols-5 md:grid-cols-6 gap-2">
          <button v-for="band in bands['4G_TDD']" :key="band.name" @click="band.locked = !band.locked"
            class="relative p-2 rounded-xl border-2 transition-all hover:scale-105"
            :class="band.locked ? 'bg-blue-50 dark:bg-blue-500/20 border-blue-500 shadow-md shadow-blue-500/20' : 'bg-slate-50 dark:bg-white/5 border-slate-200 dark:border-white/10 hover:border-blue-300 dark:hover:border-blue-500/50'">
            <div v-if="band.locked" class="absolute -top-1 -right-1 w-4 h-4 rounded-full bg-blue-500 flex items-center justify-center">
              <i class="fas fa-check text-white text-[8px]"></i>
            </div>
            <p class="text-slate-900 dark:text-white font-semibold text-sm text-center">{{ band.label }}</p>
          </button>
        </div>
      </div>

      <!-- 4G FDD -->
      <div class="mb-4">
        <div class="flex items-center space-x-2 mb-2">
          <div class="w-2 h-2 rounded-full bg-green-500"></div>
          <span class="text-slate-700 dark:text-white/80 text-xs font-medium">4G FDD</span>
          <div class="flex-1 h-px bg-slate-200 dark:bg-white/10"></div>
          <span class="text-slate-400 dark:text-white/40 text-xs">{{ bands['4G_FDD'].filter(b => b.locked).length }}/{{ bands['4G_FDD'].length }}</span>
        </div>
        <div class="grid grid-cols-3 sm:grid-cols-4 md:grid-cols-5 gap-2">
          <button v-for="band in bands['4G_FDD']" :key="band.name" @click="band.locked = !band.locked"
            class="relative p-2 rounded-xl border-2 transition-all hover:scale-105"
            :class="band.locked ? 'bg-green-50 dark:bg-green-500/20 border-green-500 shadow-md shadow-green-500/20' : 'bg-slate-50 dark:bg-white/5 border-slate-200 dark:border-white/10 hover:border-green-300 dark:hover:border-green-500/50'">
            <div v-if="band.locked" class="absolute -top-1 -right-1 w-4 h-4 rounded-full bg-green-500 flex items-center justify-center">
              <i class="fas fa-check text-white text-[8px]"></i>
            </div>
            <p class="text-slate-900 dark:text-white font-semibold text-sm text-center">{{ band.label }}</p>
          </button>
        </div>
      </div>

      <!-- 5G -->
      <div>
        <div class="flex items-center space-x-2 mb-2">
          <div class="w-2 h-2 rounded-full bg-purple-500"></div>
          <span class="text-slate-700 dark:text-white/80 text-xs font-medium">5G</span>
          <div class="flex-1 h-px bg-slate-200 dark:bg-white/10"></div>
          <span class="text-slate-400 dark:text-white/40 text-xs">{{ bands['5G'].filter(b => b.locked).length }}/{{ bands['5G'].length }}</span>
        </div>
        <div class="grid grid-cols-4 sm:grid-cols-6 md:grid-cols-8 gap-2">
          <button v-for="band in bands['5G']" :key="band.name" @click="band.locked = !band.locked"
            class="relative p-2 rounded-xl border-2 transition-all hover:scale-105"
            :class="band.locked ? 'bg-purple-50 dark:bg-purple-500/20 border-purple-500 shadow-md shadow-purple-500/20' : 'bg-slate-50 dark:bg-white/5 border-slate-200 dark:border-white/10 hover:border-purple-300 dark:hover:border-purple-500/50'">
            <div v-if="band.locked" class="absolute -top-1 -right-1 w-4 h-4 rounded-full bg-purple-500 flex items-center justify-center">
              <i class="fas fa-check text-white text-[8px]"></i>
            </div>
            <p class="text-slate-900 dark:text-white font-semibold text-sm text-center">{{ band.label }}</p>
          </button>
        </div>
      </div>
    </div>


    <!-- 小区锁定 -->
    <div class="rounded-2xl bg-white/95 dark:bg-white/5 backdrop-blur border border-slate-200/60 dark:border-white/10 p-4 shadow-lg shadow-slate-200/40 dark:shadow-black/20 hover:shadow-xl hover:shadow-slate-300/50 dark:hover:shadow-black/30 transition-all duration-300">
      <!-- 标题栏 -->
      <div class="flex items-center justify-between mb-4">
        <div class="flex items-center space-x-3">
          <div class="w-10 h-10 rounded-xl bg-gradient-to-br from-cyan-500 to-blue-500 flex items-center justify-center shadow-lg shadow-cyan-500/30">
            <i class="fas fa-tower-cell text-white"></i>
          </div>
          <div>
            <h3 class="text-slate-900 dark:text-white font-semibold text-sm">{{ t('advanced.cellLock') }}</h3>
            <p class="text-slate-500 dark:text-white/50 text-xs">{{ t('advanced.foundCells', { count: cells.length }) }}</p>
          </div>
        </div>
        <div class="flex items-center gap-2">
          <button @click="fetchCells" :disabled="refreshing"
            class="px-3 py-1.5 bg-slate-100 dark:bg-white/10 text-slate-600 dark:text-white/70 text-xs rounded-lg hover:bg-slate-200 dark:hover:bg-white/20 transition-all disabled:opacity-50">
            <i :class="refreshing ? 'fas fa-spinner animate-spin' : 'fas fa-sync-alt'" class="mr-1"></i>
            {{ refreshing ? t('advanced.scanning') : t('advanced.scan') }}
          </button>
          <button @click="handleUnlockCell" :disabled="lockingCell"
            class="px-3 py-1.5 bg-gradient-to-r from-cyan-500 to-blue-500 text-white text-xs rounded-lg hover:shadow-lg hover:shadow-cyan-500/30 transition-all disabled:opacity-50">
            <i class="fas fa-unlock mr-1"></i>{{ t('advanced.unlock') }}
          </button>
        </div>
      </div>

      <!-- 主小区 -->
      <div v-if="servingCell" class="mb-4">
        <div class="flex items-center space-x-2 mb-2">
          <div class="w-2 h-2 rounded-full bg-green-500 animate-pulse"></div>
          <span class="text-slate-700 dark:text-white/80 text-xs font-medium">{{ t('advanced.servingCell') }}</span>
          <span class="px-2 py-0.5 bg-green-100 dark:bg-green-500/20 text-green-600 dark:text-green-400 text-[10px] rounded-full">{{ t('advanced.connected') }}</span>
        </div>
        <div class="p-3 bg-green-50 dark:bg-green-500/10 rounded-xl border border-green-200 dark:border-green-500/30">
          <div class="grid grid-cols-4 sm:grid-cols-7 gap-3">
            <div class="text-center">
              <p class="text-slate-500 dark:text-white/50 text-[10px]">{{ t('advanced.rat') }}</p>
              <p class="text-slate-900 dark:text-white font-semibold text-sm">{{ servingCell.rat }}</p>
            </div>
            <div class="text-center">
              <p class="text-slate-500 dark:text-white/50 text-[10px]">{{ t('advanced.band') }}</p>
              <p class="text-purple-600 dark:text-purple-400 font-semibold text-sm">{{ servingCell.band }}</p>
            </div>
            <div class="text-center hidden sm:block">
              <p class="text-slate-500 dark:text-white/50 text-[10px]">{{ t('advanced.arfcn') }}</p>
              <p class="text-slate-900 dark:text-white font-mono text-xs">{{ servingCell.arfcn }}</p>
            </div>
            <div class="text-center">
              <p class="text-slate-500 dark:text-white/50 text-[10px]">{{ t('advanced.pci') }}</p>
              <p class="text-slate-900 dark:text-white font-semibold text-sm">{{ servingCell.pci }}</p>
            </div>
            <div class="text-center">
              <p class="text-slate-500 dark:text-white/50 text-[10px]">RSRP</p>
              <p :class="getSignalColor(servingCell.rsrp)" class="font-semibold text-sm">{{ servingCell.rsrp?.toFixed(0) }}</p>
            </div>
            <div class="text-center hidden sm:block">
              <p class="text-slate-500 dark:text-white/50 text-[10px]">RSRQ</p>
              <p class="text-cyan-600 dark:text-cyan-400 font-semibold text-sm">{{ servingCell.rsrq?.toFixed(1) }}</p>
            </div>
            <div class="text-center hidden sm:block">
              <p class="text-slate-500 dark:text-white/50 text-[10px]">SINR</p>
              <p class="text-indigo-600 dark:text-indigo-400 font-semibold text-sm">{{ servingCell.sinr?.toFixed(1) }}</p>
            </div>
          </div>
        </div>
      </div>

      <!-- 小区列表（包含主小区和邻区对比） -->
      <div v-if="cells.length > 0">
        <div class="flex items-center space-x-2 mb-2">
          <div class="w-2 h-2 rounded-full bg-cyan-500"></div>
          <span class="text-slate-700 dark:text-white/80 text-xs font-medium">{{ t('advanced.cellList') || '小区列表' }}</span>
          <div class="flex-1 h-px bg-slate-200 dark:bg-white/10"></div>
          <span class="text-slate-400 dark:text-white/40 text-xs">{{ cells.length }}</span>
        </div>
        <div class="space-y-2">
          <div v-for="(cell, index) in cells" :key="index"
            class="group relative overflow-hidden flex items-center justify-between p-3 rounded-xl border transition-all"
            :class="cell.isServing 
              ? 'bg-green-50/50 dark:bg-green-500/5 border-green-400 dark:border-green-500/40' 
              : isRecommended(cell) 
                ? 'bg-slate-50 dark:bg-white/5 border-amber-400 dark:border-amber-500/50' 
                : 'bg-slate-50 dark:bg-white/5 border-slate-200 dark:border-white/10 hover:border-cyan-300 dark:hover:border-cyan-500/50'">
            <!-- 推荐角标（仅邻区显示） -->
            <div v-if="!cell.isServing && isRecommended(cell)" 
              class="absolute left-0 top-0 bg-gradient-to-r from-amber-500 to-orange-500 text-white text-[9px] font-bold px-1.5 py-0.5 rounded-br-lg"
              :title="t('advanced.recommendReason')">
              <i class="fas fa-star mr-0.5"></i>{{ t('advanced.recommended') }}
            </div>
            <div class="grid grid-cols-4 sm:grid-cols-6 gap-3 flex-1">
              <div class="text-center">
                <p class="text-slate-500 dark:text-white/50 text-[10px]">{{ t('advanced.rat') }}</p>
                <p class="text-slate-900 dark:text-white text-xs">{{ cell.rat }}</p>
              </div>
              <div class="text-center">
                <p class="text-slate-500 dark:text-white/50 text-[10px]">{{ t('advanced.band') }}</p>
                <p class="text-purple-600 dark:text-purple-400 text-xs">{{ cell.band }}</p>
              </div>
              <div class="text-center hidden sm:block">
                <p class="text-slate-500 dark:text-white/50 text-[10px]">{{ t('advanced.arfcn') }}</p>
                <p class="text-slate-900 dark:text-white font-mono text-xs">{{ cell.arfcn }}</p>
              </div>
              <div class="text-center">
                <p class="text-slate-500 dark:text-white/50 text-[10px]">{{ t('advanced.pci') }}</p>
                <p class="text-slate-900 dark:text-white text-xs">{{ cell.pci }}</p>
              </div>
              <div class="text-center">
                <p class="text-slate-500 dark:text-white/50 text-[10px]">RSRP</p>
                <p :class="getSignalColor(cell.rsrp)" class="text-xs">{{ cell.rsrp?.toFixed(0) }}</p>
              </div>
              <div class="text-center hidden sm:block">
                <p class="text-slate-500 dark:text-white/50 text-[10px]">SINR</p>
                <p class="text-indigo-600 dark:text-indigo-400 text-xs">{{ cell.sinr?.toFixed(1) }}</p>
              </div>
            </div>
            <button @click="handleLockCell(cell)" :disabled="lockingCell"
              class="ml-2 px-2 py-1.5 bg-cyan-500 text-white text-xs rounded-lg hover:bg-cyan-600 transition-all disabled:opacity-50 opacity-0 group-hover:opacity-100">
              <i class="fas fa-lock"></i>
            </button>
          </div>
        </div>
      </div>

      <!-- 自定义锁定 -->
      <div class="mt-4 border-t border-slate-200 dark:border-white/10 pt-4">
        <button @click="showCustomLock = !showCustomLock"
          class="w-full flex items-center justify-between p-3 bg-slate-50 dark:bg-white/5 rounded-xl hover:bg-slate-100 dark:hover:bg-white/10 transition-all duration-300">
          <div class="flex items-center space-x-2">
            <div class="w-6 h-6 rounded-lg bg-gradient-to-br from-amber-500 to-orange-500 flex items-center justify-center shadow-md shadow-amber-500/30">
              <i class="fas fa-sliders text-white text-[10px]"></i>
            </div>
            <span class="text-slate-700 dark:text-white/80 text-sm font-medium">{{ t('advanced.customLock') }}</span>
          </div>
          <i class="fas fa-chevron-down text-slate-400 dark:text-white/40 text-xs transition-transform duration-300"
            :class="{ 'rotate-180': showCustomLock }"></i>
        </button>
        
        <transition
          enter-active-class="transition-all duration-300 ease-out"
          leave-active-class="transition-all duration-200 ease-in"
          enter-from-class="opacity-0 max-h-0"
          enter-to-class="opacity-100 max-h-48"
          leave-from-class="opacity-100 max-h-48"
          leave-to-class="opacity-0 max-h-0">
          <div v-if="showCustomLock" class="overflow-hidden">
            <div class="mt-3 p-3 bg-slate-50 dark:bg-white/5 rounded-xl">
              <!-- 手机端：垂直布局 -->
              <div class="sm:hidden space-y-3">
                <div class="flex gap-2">
                  <button type="button" @click="customLockForm.rat = 'LTE'"
                    class="flex-1 py-2.5 text-xs font-semibold rounded-lg transition-all duration-200"
                    :class="customLockForm.rat === 'LTE' 
                      ? 'bg-gradient-to-r from-amber-500 to-orange-500 text-white shadow-lg shadow-amber-500/30' 
                      : 'bg-white dark:bg-white/10 text-slate-500 dark:text-white/50 border border-slate-200 dark:border-white/10'">
                    4G LTE
                  </button>
                  <button type="button" @click="customLockForm.rat = 'NR'"
                    class="flex-1 py-2.5 text-xs font-semibold rounded-lg transition-all duration-200"
                    :class="customLockForm.rat === 'NR' 
                      ? 'bg-gradient-to-r from-amber-500 to-orange-500 text-white shadow-lg shadow-amber-500/30' 
                      : 'bg-white dark:bg-white/10 text-slate-500 dark:text-white/50 border border-slate-200 dark:border-white/10'">
                    5G NR
                  </button>
                </div>
                <div class="flex gap-2">
                  <input v-model="customLockForm.arfcn" type="text" :placeholder="t('advanced.arfcnPlaceholder')"
                    class="flex-1 px-3 py-2.5 bg-white dark:bg-white/10 border border-slate-200 dark:border-white/10 rounded-lg text-slate-900 dark:text-white text-sm placeholder-slate-400 dark:placeholder-white/40 focus:ring-2 focus:ring-amber-500/50 focus:border-amber-500 transition-all">
                  <input v-model="customLockForm.pci" type="text" placeholder="PCI"
                    class="w-24 px-3 py-2.5 bg-white dark:bg-white/10 border border-slate-200 dark:border-white/10 rounded-lg text-slate-900 dark:text-white text-sm placeholder-slate-400 dark:placeholder-white/40 focus:ring-2 focus:ring-amber-500/50 focus:border-amber-500 transition-all">
                </div>
                <button @click="handleCustomLockCell" :disabled="customLocking"
                  class="w-full py-2.5 bg-gradient-to-r from-amber-500 to-orange-500 text-white text-sm font-semibold rounded-lg hover:shadow-lg hover:shadow-amber-500/30 active:scale-[0.98] transition-all duration-200 disabled:opacity-50">
                  <i :class="customLocking ? 'fas fa-spinner fa-spin' : 'fas fa-lock'" class="mr-1.5"></i>
                  {{ customLocking ? t('advanced.locking') : t('advanced.lockCellBtn') }}
                </button>
              </div>
              <!-- 电脑端：横向布局 -->
              <div class="hidden sm:flex items-center gap-2">
                <div class="flex gap-1">
                  <button type="button" @click="customLockForm.rat = 'LTE'"
                    class="px-3 py-2.5 text-xs font-semibold rounded-lg transition-all duration-200"
                    :class="customLockForm.rat === 'LTE' 
                      ? 'bg-gradient-to-r from-amber-500 to-orange-500 text-white shadow-lg shadow-amber-500/30' 
                      : 'bg-white dark:bg-white/10 text-slate-500 dark:text-white/50 border border-slate-200 dark:border-white/10'">
                    4G
                  </button>
                  <button type="button" @click="customLockForm.rat = 'NR'"
                    class="px-3 py-2.5 text-xs font-semibold rounded-lg transition-all duration-200"
                    :class="customLockForm.rat === 'NR' 
                      ? 'bg-gradient-to-r from-amber-500 to-orange-500 text-white shadow-lg shadow-amber-500/30' 
                      : 'bg-white dark:bg-white/10 text-slate-500 dark:text-white/50 border border-slate-200 dark:border-white/10'">
                    5G
                  </button>
                </div>
                <input v-model="customLockForm.arfcn" type="text" :placeholder="t('advanced.arfcnPlaceholder')"
                  class="flex-1 px-3 py-2.5 bg-white dark:bg-white/10 border border-slate-200 dark:border-white/10 rounded-lg text-slate-900 dark:text-white text-sm placeholder-slate-400 dark:placeholder-white/40 focus:ring-2 focus:ring-amber-500/50 focus:border-amber-500 transition-all">
                <input v-model="customLockForm.pci" type="text" placeholder="PCI"
                  class="w-28 px-3 py-2.5 bg-white dark:bg-white/10 border border-slate-200 dark:border-white/10 rounded-lg text-slate-900 dark:text-white text-sm placeholder-slate-400 dark:placeholder-white/40 focus:ring-2 focus:ring-amber-500/50 focus:border-amber-500 transition-all">
                <button @click="handleCustomLockCell" :disabled="customLocking"
                  class="px-5 py-2.5 bg-gradient-to-r from-amber-500 to-orange-500 text-white text-sm font-semibold rounded-lg hover:shadow-lg hover:shadow-amber-500/30 active:scale-[0.98] transition-all duration-200 disabled:opacity-50 whitespace-nowrap">
                  <i :class="customLocking ? 'fas fa-spinner fa-spin' : 'fas fa-lock'" class="mr-1.5"></i>
                  {{ customLocking ? t('advanced.locking') : t('advanced.lock') }}
                </button>
              </div>
            </div>
          </div>
        </transition>
      </div>

      <!-- 空状态 -->
      <div v-if="!refreshing && cells.length === 0" class="text-center py-8">
        <div class="w-16 h-16 mx-auto mb-3 bg-slate-100 dark:bg-white/10 rounded-full flex items-center justify-center">
          <i class="fas fa-tower-cell text-slate-400 dark:text-white/40 text-2xl"></i>
        </div>
        <p class="text-slate-600 dark:text-white/60 text-sm mb-1">{{ t('advanced.noCellsFound') }}</p>
        <p class="text-slate-400 dark:text-white/40 text-xs">{{ t('advanced.clickScan') }}</p>
      </div>

      <!-- 扫描动画 -->
      <div v-if="refreshing && cells.length === 0" class="text-center py-8">
        <div class="relative w-16 h-16 mx-auto mb-3">
          <div class="w-16 h-16 bg-cyan-100 dark:bg-cyan-500/20 rounded-full flex items-center justify-center">
            <i class="fas fa-tower-cell text-cyan-500 text-2xl"></i>
          </div>
          <div class="absolute inset-0 border-2 border-cyan-400/30 border-t-cyan-400 rounded-full animate-spin"></div>
        </div>
        <p class="text-cyan-600 dark:text-cyan-400 text-sm">{{ t('advanced.scanningCells') }}</p>
      </div>
    </div>
  </div>
</template>
