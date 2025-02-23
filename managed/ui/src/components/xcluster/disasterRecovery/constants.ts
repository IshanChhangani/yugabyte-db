export const DrConfigActions = {
  CREATE: 'createDrConfig',
  DELETE: 'deleteDrConfig',
  EDIT: 'editDrConfig',
  EDIT_TARGET: 'editDrConfigTarget',
  SWITCHOVER: 'switchover',
  FAILOVER: 'failover'
} as const;
export type DrConfigActions = typeof DrConfigActions[keyof typeof DrConfigActions];

export const DurationUnit = {
  SECOND: 'second',
  MINUTE: 'minute',
  HOUR: 'hour'
} as const;
export type DurationUnit = typeof DurationUnit[keyof typeof DurationUnit];

/**
 * Map from RPO units to milliseconds.
 */
export const DURATION_UNIT_TO_MS = {
  [DurationUnit.SECOND]: 1000,
  [DurationUnit.MINUTE]: 60 * 1000,
  [DurationUnit.HOUR]: 60 * 60 * 1000
} as const;

/**
 * Standard width for all dropdown select components in the DR workflow.
 */
export const DR_DROPDOWN_SELECT_INPUT_WIDTH_PX = 350;

export const DOCS_URL_ACTIVE_ACTIVE_SINGLE_MASTER =
  'https://docs.yugabyte.com/preview/develop/build-global-apps/active-active-single-master/';
export const DOCS_URL_DR_REPLICA_SELECTION_LIMITATIONS =
  'https://docs.yugabyte.com/preview/yugabyte-platform/create-deployments/async-replication-platform/#limitations';
