import { fetchJson } from "../../lib/client";
import { formatMoney } from "../../lib/format";

export function Dashboard() {
  return formatMoney(fetchJson("/widgets"));
}
