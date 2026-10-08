import {
  ResponsiveContainer,
  AreaChart,
  Area,
  CartesianGrid,
  XAxis,
  YAxis,
  Tooltip,
  Legend,
} from "recharts";
import type { Point } from "../services/api";
export function MetricChart({
  title,
  subtitle,
  data,
  lines,
}: {
  title: string;
  subtitle: string;
  data: Point[];
  lines: { key: keyof Point; label: string; color: string }[];
}) {
  return (
    <section className="panel chart">
      <div className="panel-heading">
        <div>
          <h3>{title}</h3>
          <p>{subtitle}</p>
        </div>
        <span className="chart-dot">LIVE</span>
      </div>
      <div className="chart-body">
        {data.length === 0 ? (
          <div className="empty">Waiting for gateway measurements</div>
        ) : (
          <ResponsiveContainer width="100%" height="100%">
            <AreaChart
              data={data}
              margin={{ top: 12, right: 8, left: -18, bottom: 0 }}
            >
              <defs>
                {lines.map((l) => (
                  <linearGradient
                    key={l.key}
                    id={"fill-" + l.key}
                    x1="0"
                    y1="0"
                    x2="0"
                    y2="1"
                  >
                    <stop offset="0%" stopColor={l.color} stopOpacity={0.2} />
                    <stop offset="100%" stopColor={l.color} stopOpacity={0} />
                  </linearGradient>
                ))}
              </defs>
              <CartesianGrid
                stroke="#202b3b"
                vertical={false}
                strokeDasharray="3 4"
              />
              <XAxis
                dataKey="time"
                stroke="#64748b"
                fontSize={10}
                minTickGap={60}
                tickLine={false}
                axisLine={false}
              />
              <YAxis
                stroke="#64748b"
                fontSize={10}
                tickLine={false}
                axisLine={false}
              />
              <Tooltip
                contentStyle={{
                  background: "#111b29",
                  border: "1px solid #334155",
                  borderRadius: 8,
                }}
                formatter={(v: number) => v.toFixed(1)}
              />
              {lines.length > 1 && <Legend wrapperStyle={{ fontSize: 11 }} />}
              {lines.map((l) => (
                <Area
                  key={l.key}
                  type="monotone"
                  dataKey={l.key}
                  name={l.label}
                  stroke={l.color}
                  fill={"url(#fill-" + l.key + ")"}
                  strokeWidth={2}
                  isAnimationActive={false}
                  dot={false}
                />
              ))}
            </AreaChart>
          </ResponsiveContainer>
        )}
      </div>
    </section>
  );
}
