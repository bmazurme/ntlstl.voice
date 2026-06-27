export interface HaCommand {
  action: 'turn_on' | 'turn_off' | 'toggle' | 'set_brightness' | 'query' | 'unknown' | string;
  entity: string | null;
  value: number | null;
  response_text: string;
}

export interface HaState {
  entity_id: string;
  state: string;
  attributes?: { friendly_name?: string };
}
