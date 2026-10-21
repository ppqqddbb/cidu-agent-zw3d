using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Web.Script.Serialization;

namespace Cidu.JiavuKnowledgeSync
{
    internal sealed class JiavuDbConfig
    {
        public string Host = "";
        public int Port = 3306;
        public string Database = "";
        public string User = "";
        public string Password = "";
        public string DifyBaseUrl = "";
        public string DifyDatasetApiKey = "";
        public string DifyDatasetId = "";
        public string DifyKnowledgeGraphId = "";
        public string DifyDatasetIndexingTechnique = "";
        public string DifyKnowledgeBaseDocumentPrefix = "";
        public string JyasmServiceBaseUrl = "";
        public string JyasmServiceToken = "";
        public bool JyasmServiceTriggerRefresh = true;
        public string JyasmServicePublicBaseUrl = "";
        public int JyasmServiceTimeoutMs = 900000;
    }

    internal sealed class ColumnInfo
    {
        public string TableName = "";
        public string Name = "";
        public string DataType = "";
    }

    internal sealed class RecordInfo
    {
        public string Key = "";
        public string NodeId = "";
        public string Fingerprint = "";
        public string TableName = "";
        public string Type = "";
        public string Name = "";
        public Dictionary<string, string> Values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        public List<CatalogNodeInfo> CatalogPath = new List<CatalogNodeInfo>();
        public List<ProductParameterInfo> Parameters = new List<ProductParameterInfo>();
        public string JyasmSourcePath = "";
        public string JyasmExtractedDir = "";
        public List<string> JyasmImageUrls = new List<string>();
        public List<GbStandardPartInfo> GbParts = new List<GbStandardPartInfo>();
        public bool JyasmEnriched;
    }

    internal sealed class CatalogNodeInfo
    {
        public string Id = "";
        public string ParentId = "";
        public string Name = "";
        public string FullPath = "";
    }

    internal sealed class ProductParameterInfo
    {
        public string Id = "";
        public string Name = "";
        public string FieldId = "";
        public string FieldName = "";
        public int Sort;
        public string Unit = "";
        public string Range = "";
        public string Remark = "";
        public string Default = "";
        public int StepPage;
        public string Image = "";
        public string ImageUrl = "";
        // jyasm 解出的可见性：always / conditional / hidden。
        // hidden 在 EnrichFromJyasmService 已经被过滤，这里只会出现 always / conditional。
        public string Visibility = "always";
        // 条件可见参数的原始 type_expr，形如 "{$IF$ <grid12>=1:Edit | 1=1:Hidden $ENDIF$}"；
        // 前端展示时可作为 hover 提示，帮用户理解为什么这个参数被标为"条件显示"。
        public string TypeExpr = "";
    }

    internal sealed class GbStandardPartInfo
    {
        public string Gb = "";
        public string Meaning = "";
        public string Spec = "";
        public string Material = "";
    }

    internal sealed class MysqlClient : IDisposable
    {
        private const uint ClientLongPassword = 0x00000001;
        private const uint ClientLongFlag = 0x00000004;
        private const uint ClientConnectWithDb = 0x00000008;
        private const uint ClientProtocol41 = 0x00000200;
        private const uint ClientTransactions = 0x00002000;
        private const uint ClientSecureConnection = 0x00008000;
        private const uint ClientMultiResults = 0x00020000;
        private const uint ClientPluginAuth = 0x00080000;

        private readonly string host_;
        private readonly int port_;
        private readonly string user_;
        private readonly string password_;
        private readonly string database_;
        private TcpClient tcp_;
        private NetworkStream stream_;

        public MysqlClient(string host, int port, string user, string password, string database)
        {
            host_ = host;
            port_ = port;
            user_ = user;
            password_ = password;
            database_ = database;
        }

        public void Connect()
        {
            tcp_ = new TcpClient();
            tcp_.ReceiveTimeout = 900000;
            tcp_.SendTimeout = 900000;
            tcp_.Connect(host_, port_);
            stream_ = tcp_.GetStream();

            byte[] handshake = ReadPacket();
            HandshakeInfo info = ParseHandshake(handshake);
            string plugin = string.IsNullOrEmpty(info.AuthPluginName) ? "mysql_native_password" : info.AuthPluginName;
            byte[] auth = ScramblePassword(password_, info.AuthSeed, plugin);

            uint flags = ClientLongPassword |
                         ClientLongFlag |
                         ClientProtocol41 |
                         ClientTransactions |
                         ClientSecureConnection |
                         ClientMultiResults |
                         ClientPluginAuth;
            if (!string.IsNullOrEmpty(database_))
            {
                flags |= ClientConnectWithDb;
            }

            MemoryStream response = new MemoryStream();
            WriteUInt32(response, flags);
            WriteUInt32(response, 16 * 1024 * 1024);
            response.WriteByte(45);
            response.Write(new byte[23], 0, 23);
            WriteNullTerminated(response, user_);
            response.WriteByte((byte)auth.Length);
            response.Write(auth, 0, auth.Length);
            if (!string.IsNullOrEmpty(database_))
            {
                WriteNullTerminated(response, database_);
            }
            WriteNullTerminated(response, plugin);
            WritePacket(response.ToArray(), 1);

            byte[] authResult = ReadPacket();
            HandleAuthResult(authResult, info.AuthSeed);
        }

        public List<string[]> Query(string sql, out List<string> columnNames)
        {
            byte[] query = Encoding.UTF8.GetBytes(sql);
            byte[] payload = new byte[query.Length + 1];
            payload[0] = 0x03;
            Buffer.BlockCopy(query, 0, payload, 1, query.Length);
            WritePacket(payload, 0);

            byte[] first = ReadPacket();
            if (first.Length == 0)
            {
                throw new InvalidOperationException("Empty MySQL response.");
            }
            if (first[0] == 0xff)
            {
                throw new InvalidOperationException(ParseError(first));
            }
            if (first[0] == 0x00)
            {
                columnNames = new List<string>();
                return new List<string[]>();
            }

            int offset = 0;
            ulong columnCount = ReadLengthEncodedInteger(first, ref offset);
            columnNames = new List<string>();
            for (ulong i = 0; i < columnCount; ++i)
            {
                byte[] columnPacket = ReadPacket();
                columnNames.Add(ParseColumnName(columnPacket));
            }

            ReadEofPacket();

            List<string[]> rows = new List<string[]>();
            while (true)
            {
                byte[] rowPacket = ReadPacket();
                if (IsEof(rowPacket))
                {
                    break;
                }
                if (rowPacket.Length > 0 && rowPacket[0] == 0xff)
                {
                    throw new InvalidOperationException(ParseError(rowPacket));
                }
                rows.Add(ParseRow(rowPacket, (int)columnCount));
            }
            return rows;
        }

        public List<string[]> Query(string sql)
        {
            List<string> ignored;
            return Query(sql, out ignored);
        }

        public void Dispose()
        {
            if (stream_ != null)
            {
                stream_.Dispose();
            }
            if (tcp_ != null)
            {
                tcp_.Close();
            }
        }

        private void HandleAuthResult(byte[] packet, byte[] seed)
        {
            if (packet.Length == 0)
            {
                throw new InvalidOperationException("Empty MySQL auth response.");
            }
            if (packet[0] == 0x00)
            {
                return;
            }
            if (packet[0] == 0xff)
            {
                throw new InvalidOperationException(ParseError(packet));
            }
            if (packet[0] == 0xfe && packet.Length > 1)
            {
                int pos = 1;
                string plugin = ReadNullTerminatedString(packet, ref pos);
                byte[] switchSeed = new byte[Math.Max(0, packet.Length - pos)];
                Buffer.BlockCopy(packet, pos, switchSeed, 0, switchSeed.Length);
                byte[] auth = ScramblePassword(password_, switchSeed.Length > 0 ? switchSeed : seed, plugin);
                WritePacket(auth, 3);
                HandleAuthResult(ReadPacket(), switchSeed.Length > 0 ? switchSeed : seed);
                return;
            }
            if (packet[0] == 0x01 && packet.Length > 1)
            {
                if (packet[1] == 0x03)
                {
                    HandleAuthResult(ReadPacket(), seed);
                    return;
                }
                if (packet[1] == 0x04)
                {
                    throw new InvalidOperationException(
                        "MySQL caching_sha2_password full authentication is not supported. Please use a mysql_native_password read-only user for Jiavu sync.");
                }
            }
            throw new InvalidOperationException("Unsupported MySQL auth response.");
        }

        private byte[] ReadPacket()
        {
            byte[] header = ReadExact(4);
            int length = header[0] | (header[1] << 8) | (header[2] << 16);
            return ReadExact(length);
        }

        private void WritePacket(byte[] payload, int sequence)
        {
            byte[] header = new byte[4];
            header[0] = (byte)(payload.Length & 0xff);
            header[1] = (byte)((payload.Length >> 8) & 0xff);
            header[2] = (byte)((payload.Length >> 16) & 0xff);
            header[3] = (byte)(sequence & 0xff);
            stream_.Write(header, 0, header.Length);
            stream_.Write(payload, 0, payload.Length);
            stream_.Flush();
        }

        private byte[] ReadExact(int count)
        {
            byte[] buffer = new byte[count];
            int offset = 0;
            while (offset < count)
            {
                int read = stream_.Read(buffer, offset, count - offset);
                if (read <= 0)
                {
                    throw new IOException("Unexpected end of MySQL stream.");
                }
                offset += read;
            }
            return buffer;
        }

        private void ReadEofPacket()
        {
            byte[] packet = ReadPacket();
            if (packet.Length > 0 && packet[0] == 0xff)
            {
                throw new InvalidOperationException(ParseError(packet));
            }
        }

        private static bool IsEof(byte[] packet)
        {
            return packet.Length > 0 && packet[0] == 0xfe && packet.Length < 9;
        }

        private static string ParseColumnName(byte[] packet)
        {
            int pos = 0;
            ReadLengthEncodedString(packet, ref pos);
            ReadLengthEncodedString(packet, ref pos);
            ReadLengthEncodedString(packet, ref pos);
            ReadLengthEncodedString(packet, ref pos);
            return ReadLengthEncodedString(packet, ref pos);
        }

        private static string[] ParseRow(byte[] packet, int columnCount)
        {
            string[] row = new string[columnCount];
            int pos = 0;
            for (int i = 0; i < columnCount; ++i)
            {
                if (pos < packet.Length && packet[pos] == 0xfb)
                {
                    ++pos;
                    row[i] = "";
                    continue;
                }
                ulong length = ReadLengthEncodedInteger(packet, ref pos);
                if (length > int.MaxValue || pos + (int)length > packet.Length)
                {
                    row[i] = "";
                    pos = packet.Length;
                    continue;
                }
                row[i] = Encoding.UTF8.GetString(packet, pos, (int)length);
                pos += (int)length;
            }
            return row;
        }

        private static string ParseError(byte[] packet)
        {
            if (packet.Length < 3)
            {
                return "MySQL returned an error.";
            }
            int code = packet[1] | (packet[2] << 8);
            string message = packet.Length > 9
                ? Encoding.UTF8.GetString(packet, 9, packet.Length - 9)
                : "";
            return "MySQL error " + code + ": " + message;
        }

        private static HandshakeInfo ParseHandshake(byte[] packet)
        {
            int pos = 0;
            ++pos;
            ReadNullTerminatedString(packet, ref pos);
            pos += 4;
            byte[] seed1 = new byte[8];
            Buffer.BlockCopy(packet, pos, seed1, 0, 8);
            pos += 9;
            pos += 2;
            pos += 1;
            pos += 2;
            pos += 2;
            int authLength = packet[pos++];
            pos += 10;

            int part2Length = Math.Max(13, authLength - 8);
            part2Length = Math.Max(0, Math.Min(part2Length, packet.Length - pos));
            byte[] seed2 = new byte[part2Length];
            Buffer.BlockCopy(packet, pos, seed2, 0, part2Length);
            pos += part2Length;

            List<byte> seed = new List<byte>();
            seed.AddRange(seed1);
            foreach (byte value in seed2)
            {
                if (value != 0)
                {
                    seed.Add(value);
                }
            }

            string plugin = pos < packet.Length ? ReadNullTerminatedString(packet, ref pos) : "";
            return new HandshakeInfo { AuthSeed = seed.ToArray(), AuthPluginName = plugin };
        }

        private static byte[] ScramblePassword(string password, byte[] seed, string plugin)
        {
            if (string.IsNullOrEmpty(password))
            {
                return new byte[0];
            }
            if (plugin == "caching_sha2_password")
            {
                SHA256 sha = SHA256.Create();
                byte[] stage1 = sha.ComputeHash(Encoding.UTF8.GetBytes(password));
                byte[] stage2 = sha.ComputeHash(stage1);
                byte[] merged = new byte[stage2.Length + seed.Length];
                Buffer.BlockCopy(stage2, 0, merged, 0, stage2.Length);
                Buffer.BlockCopy(seed, 0, merged, stage2.Length, seed.Length);
                byte[] stage3 = sha.ComputeHash(merged);
                return Xor(stage1, stage3);
            }

            SHA1 sha1 = SHA1.Create();
            byte[] passwordHash = sha1.ComputeHash(Encoding.UTF8.GetBytes(password));
            byte[] doubleHash = sha1.ComputeHash(passwordHash);
            byte[] combined = new byte[seed.Length + doubleHash.Length];
            Buffer.BlockCopy(seed, 0, combined, 0, seed.Length);
            Buffer.BlockCopy(doubleHash, 0, combined, seed.Length, doubleHash.Length);
            byte[] seedHash = sha1.ComputeHash(combined);
            return Xor(passwordHash, seedHash);
        }

        private static byte[] Xor(byte[] left, byte[] right)
        {
            byte[] output = new byte[left.Length];
            for (int i = 0; i < left.Length; ++i)
            {
                output[i] = (byte)(left[i] ^ right[i]);
            }
            return output;
        }

        private static void WriteUInt32(Stream stream, uint value)
        {
            stream.WriteByte((byte)(value & 0xff));
            stream.WriteByte((byte)((value >> 8) & 0xff));
            stream.WriteByte((byte)((value >> 16) & 0xff));
            stream.WriteByte((byte)((value >> 24) & 0xff));
        }

        private static void WriteNullTerminated(Stream stream, string value)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(value ?? "");
            stream.Write(bytes, 0, bytes.Length);
            stream.WriteByte(0);
        }

        private static string ReadNullTerminatedString(byte[] packet, ref int pos)
        {
            int start = pos;
            while (pos < packet.Length && packet[pos] != 0)
            {
                ++pos;
            }
            string value = Encoding.UTF8.GetString(packet, start, pos - start);
            if (pos < packet.Length)
            {
                ++pos;
            }
            return value;
        }

        private static string ReadLengthEncodedString(byte[] packet, ref int pos)
        {
            ulong length = ReadLengthEncodedInteger(packet, ref pos);
            if (length > int.MaxValue || pos + (int)length > packet.Length)
            {
                return "";
            }
            string value = Encoding.UTF8.GetString(packet, pos, (int)length);
            pos += (int)length;
            return value;
        }

        private static ulong ReadLengthEncodedInteger(byte[] packet, ref int pos)
        {
            if (pos >= packet.Length)
            {
                return 0;
            }
            byte first = packet[pos++];
            if (first < 0xfb)
            {
                return first;
            }
            if (first == 0xfc)
            {
                ulong value = (ulong)(packet[pos] | (packet[pos + 1] << 8));
                pos += 2;
                return value;
            }
            if (first == 0xfd)
            {
                ulong value = (ulong)(packet[pos] | (packet[pos + 1] << 8) | (packet[pos + 2] << 16));
                pos += 3;
                return value;
            }
            if (first == 0xfe)
            {
                ulong value = 0;
                for (int i = 0; i < 8; ++i)
                {
                    value |= ((ulong)packet[pos + i]) << (8 * i);
                }
                pos += 8;
                return value;
            }
            return 0;
        }

        private sealed class HandshakeInfo
        {
            public byte[] AuthSeed = new byte[0];
            public string AuthPluginName = "";
        }
    }

    internal static class Program
    {
        private static readonly Encoding Utf8NoBom = new UTF8Encoding(false);
        private static readonly string[] SystemDatabases = { "information_schema", "mysql", "performance_schema", "sys" };
        private const int RowLimitPerTable = 20000;

        private static int Main(string[] args)
        {
            Console.OutputEncoding = Encoding.UTF8;
            try
            {
                string configPath = GetArg(args, "--config", "cidu_zw3d_config.json");
                string outDir = GetArg(args, "--out", Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "knowledge"));
                JiavuDbConfig config = LoadConfig(configPath);
                ValidateConfig(config);

                Directory.CreateDirectory(outDir);
                SyncReport report = Sync(config, outDir);
                Console.WriteLine(report.ToJson());
                return report.Ok ? 0 : 2;
            }
            catch (Exception ex)
            {
                Console.WriteLine("{\"ok\":false,\"message\":\"" + JsonEscape(ex.Message) + "\"}");
                return 1;
            }
        }

        private static SyncReport Sync(JiavuDbConfig config, string outDir)
        {
            using (MysqlClient client = new MysqlClient(config.Host, config.Port, config.User, config.Password, config.Database))
            {
                client.Connect();
                string database = string.IsNullOrWhiteSpace(config.Database) ? DetectDatabase(client) : config.Database.Trim();
                client.Query("USE " + QuoteIdentifier(database));

                List<ColumnInfo> columns = LoadColumns(client, database);
                Dictionary<string, List<ColumnInfo>> tables = columns
                    .GroupBy(c => c.TableName, StringComparer.OrdinalIgnoreCase)
                    .ToDictionary(g => g.Key, g => g.ToList(), StringComparer.OrdinalIgnoreCase);
                List<string> warnings = new List<string>();
                List<RecordInfo> records = LoadParameterizedDesignModels(client, database, tables, warnings);

                EnrichFromJyasmService(config, records, warnings);
                foreach (RecordInfo record in records)
                {
                    record.Fingerprint = HashHex(
                        record.Key + "|" + FlattenValues(record.Values) +
                        "|" + FlattenParameters(record.Parameters) +
                        "|" + FlattenGbParts(record.GbParts) +
                        "|" + record.JyasmSourcePath);
                }

                string knowledgeBasePath = Path.Combine(outDir, "jiavu_knowledge_base.jsonl");
                string graphPath = Path.Combine(outDir, "jiavu_knowledge_graph.json");
                string manifestPath = Path.Combine(outDir, "jiavu_manifest.json");
                string deltaPath = Path.Combine(outDir, "jiavu_delta.json");

                Dictionary<string, string> oldFingerprints = ReadManifestFingerprints(manifestPath);
                DeltaInfo delta = BuildDelta(records, oldFingerprints);

                WriteKnowledgeBase(knowledgeBasePath, database, records);
                WriteGraph(graphPath, database, records);
                WriteManifest(manifestPath, config, database, records);
                WriteDelta(deltaPath, database, delta);

                DifySyncResult difySync = SyncToDifyIfConfigured(config, knowledgeBasePath, graphPath);

                SyncReport report = new SyncReport();
                report.Ok = difySync.Ok;
                report.Message = BuildReportMessage(records.Count, delta, difySync);
                report.Database = database;
                report.RecordCount = records.Count;
                report.Added = delta.Added;
                report.Updated = delta.Updated;
                report.Removed = delta.Removed;
                report.Unchanged = delta.Unchanged;
                report.KnowledgeBasePath = knowledgeBasePath;
                report.GraphPath = graphPath;
                report.ManifestPath = manifestPath;
                report.DeltaPath = deltaPath;
                report.DifyKnowledgeBaseSynced = difySync.KnowledgeBaseSynced;
                report.DifyKnowledgeGraphSynced = difySync.KnowledgeGraphSynced;
                report.DifyKnowledgeBaseStatusCode = difySync.KnowledgeBaseStatusCode;
                report.DifyKnowledgeGraphStatusCode = difySync.KnowledgeGraphStatusCode;
                report.DifyError = difySync.Error;
                report.Warnings = warnings;
                File.WriteAllText(Path.Combine(outDir, "jiavu_sync_report.json"), report.ToJson(), Utf8NoBom);
                return report;
            }
        }

        private static string DetectDatabase(MysqlClient client)
        {
            List<string[]> rows = client.Query("SHOW DATABASES");
            List<string> names = rows.Select(r => r.Length > 0 ? r[0] : "")
                .Where(n => !string.IsNullOrWhiteSpace(n))
                .Where(n => !SystemDatabases.Contains(n, StringComparer.OrdinalIgnoreCase))
                .ToList();
            if (names.Count == 0)
            {
                throw new InvalidOperationException("未找到可同步的佳鱼业务数据库，请在配置文件填写 jiavu_db_name。");
            }
            string preferred = names.FirstOrDefault(n =>
                ContainsAny(Normalize(n), "jiavu", "jia", "jy", "product", "model", "param"));
            return string.IsNullOrEmpty(preferred) ? names[0] : preferred;
        }

        private static List<ColumnInfo> LoadColumns(MysqlClient client, string database)
        {
            string sql =
                "SELECT TABLE_NAME, COLUMN_NAME, DATA_TYPE FROM INFORMATION_SCHEMA.COLUMNS " +
                "WHERE TABLE_SCHEMA='" + SqlEscape(database) + "' ORDER BY TABLE_NAME, ORDINAL_POSITION";
            List<string[]> rows = client.Query(sql);
            List<ColumnInfo> columns = new List<ColumnInfo>();
            foreach (string[] row in rows)
            {
                if (row.Length < 3)
                {
                    continue;
                }
                columns.Add(new ColumnInfo { TableName = row[0], Name = row[1], DataType = row[2] });
            }
            if (columns.Count == 0)
            {
                throw new InvalidOperationException("数据库中没有读取到表结构。");
            }
            return columns;
        }

        private static List<RecordInfo> LoadParameterizedDesignModels(
            MysqlClient client,
            string database,
            Dictionary<string, List<ColumnInfo>> tables,
            List<string> warnings)
        {
            Dictionary<string, CatalogNodeInfo> catalogs = LoadCatalogNodes(client, database, tables, warnings);
            Dictionary<string, List<ProductParameterInfo>> parametersByProduct =
                LoadProductParameters(client, database, tables, warnings);
            List<Dictionary<string, string>> productRows = LoadTableRows(client, database, tables, "product", warnings);
            List<RecordInfo> records = new List<RecordInfo>();

            foreach (Dictionary<string, string> product in productRows)
            {
                if (!IsVisible(product))
                {
                    continue;
                }

                string productId = GetValue(product, "id", "productId", "product_id");
                string productName = GetValue(product, "name", "productName", "product_name", "modelName", "model_name").Trim();
                string productUrl = GetValue(product, "url", "source", "path").Trim();
                if (string.IsNullOrWhiteSpace(productName) || !LooksLikeProductModel(productName, productUrl))
                {
                    continue;
                }

                string key = database + ".product:id=" +
                    (string.IsNullOrWhiteSpace(productId) ? HashHex(FlattenValues(product)).Substring(0, 16) : productId);
                RecordInfo record = new RecordInfo();
                record.Key = key;
                record.NodeId = "product:" + HashHex(key).Substring(0, 24);
                record.TableName = "product";
                record.Type = "ProductModel";
                record.Name = productName;
                record.CatalogPath = BuildCatalogPath(product, catalogs, productUrl);
                record.Parameters = parametersByProduct.ContainsKey(productId)
                    ? parametersByProduct[productId]
                        .OrderBy(p => p.Sort)
                        .ThenBy(p => p.Name, StringComparer.OrdinalIgnoreCase)
                        .ToList()
                    : new List<ProductParameterInfo>();

                string catalogPath = record.CatalogPath.Count > 0
                    ? record.CatalogPath[record.CatalogPath.Count - 1].FullPath
                    : DirectoryPathFromModelPath(productUrl);
                record.Values["product_id"] = productId;
                record.Values["catalog_id"] = GetValue(product, "cid", "catalogId", "catalogID", "category_id");
                record.Values["model_name"] = productName;
                record.Values["template_name"] = productName;
                record.Values["full_node_path"] = string.IsNullOrWhiteSpace(productUrl)
                    ? CombinePath(catalogPath, productName)
                    : productUrl;
                record.Values["catalog_path"] = catalogPath;
                record.Values["parameter_count"] = record.Parameters.Count.ToString(System.Globalization.CultureInfo.InvariantCulture);
                record.Values["parameter_names"] = string.Join("；", record.Parameters.Select(p => p.Name).ToArray());
                records.Add(record);
            }

            AssignDocumentNames(records);
            foreach (RecordInfo record in records)
            {
                record.Fingerprint = HashHex(record.Key + "|" + FlattenValues(record.Values) + "|" + FlattenParameters(record.Parameters));
            }
            if (records.Count == 0)
            {
                warnings.Add("未读取到佳鱼参数化设计产品模型，请确认 product 表中存在可见的 .jyasm 或 .jypar 模型。");
            }
            return records.OrderBy(r => GetValue(r.Values, "full_node_path"), StringComparer.OrdinalIgnoreCase).ToList();
        }

        private static Dictionary<string, CatalogNodeInfo> LoadCatalogNodes(
            MysqlClient client,
            string database,
            Dictionary<string, List<ColumnInfo>> tables,
            List<string> warnings)
        {
            Dictionary<string, CatalogNodeInfo> catalogs = new Dictionary<string, CatalogNodeInfo>(StringComparer.OrdinalIgnoreCase);
            foreach (Dictionary<string, string> row in LoadTableRows(client, database, tables, "catalog", warnings))
            {
                if (!IsVisible(row))
                {
                    continue;
                }
                string id = GetValue(row, "id", "catalogId", "catalogID");
                if (string.IsNullOrWhiteSpace(id))
                {
                    continue;
                }
                CatalogNodeInfo node = new CatalogNodeInfo();
                node.Id = id;
                node.ParentId = GetValue(row, "pid", "parentId", "parent_id");
                node.Name = GetValue(row, "name", "catalogName", "catalog_name");
                node.FullPath = GetValue(row, "source", "url", "path");
                catalogs[id] = node;
            }

            foreach (CatalogNodeInfo node in catalogs.Values)
            {
                if (string.IsNullOrWhiteSpace(node.FullPath))
                {
                    node.FullPath = BuildCatalogFullPath(node, catalogs, 0);
                }
            }
            return catalogs;
        }

        private static Dictionary<string, List<ProductParameterInfo>> LoadProductParameters(
            MysqlClient client,
            string database,
            Dictionary<string, List<ColumnInfo>> tables,
            List<string> warnings)
        {
            Dictionary<string, string> paramNames = LoadIdNameMap(
                client,
                database,
                tables,
                "productparam",
                new string[] { "paramId", "id" },
                new string[] { "paramName", "name" },
                warnings);
            Dictionary<string, string> fieldNames = LoadIdNameMap(
                client,
                database,
                tables,
                "productfield",
                new string[] { "fieldId", "id" },
                new string[] { "innerName", "fieldName", "name" },
                warnings);
            Dictionary<string, List<ProductParameterInfo>> byProduct =
                new Dictionary<string, List<ProductParameterInfo>>(StringComparer.OrdinalIgnoreCase);
            Dictionary<string, HashSet<string>> seen = new Dictionary<string, HashSet<string>>(StringComparer.OrdinalIgnoreCase);

            foreach (Dictionary<string, string> row in LoadTableRows(client, database, tables, "productindex", warnings))
            {
                string productId = GetValue(row, "productId", "product_id");
                string paramId = GetValue(row, "paramId", "param_id");
                if (string.IsNullOrWhiteSpace(productId) || string.IsNullOrWhiteSpace(paramId))
                {
                    continue;
                }

                string paramName = paramNames.ContainsKey(paramId) ? paramNames[paramId] : "";
                if (string.IsNullOrWhiteSpace(paramName))
                {
                    continue;
                }

                string fieldId = GetValue(row, "fieldId", "field_id");
                string dedupeKey = paramId + "|" + fieldId;
                if (!seen.ContainsKey(productId))
                {
                    seen[productId] = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                }
                if (seen[productId].Contains(dedupeKey))
                {
                    continue;
                }
                seen[productId].Add(dedupeKey);

                ProductParameterInfo parameter = new ProductParameterInfo();
                parameter.Id = paramId;
                parameter.Name = paramName;
                parameter.FieldId = fieldId;
                parameter.FieldName = fieldNames.ContainsKey(fieldId) ? fieldNames[fieldId] : "";
                parameter.Sort = ParseInt(GetValue(row, "sort", "order", "index"), 0);

                if (!byProduct.ContainsKey(productId))
                {
                    byProduct[productId] = new List<ProductParameterInfo>();
                }
                byProduct[productId].Add(parameter);
            }
            return byProduct;
        }

        private static Dictionary<string, string> LoadIdNameMap(
            MysqlClient client,
            string database,
            Dictionary<string, List<ColumnInfo>> tables,
            string tableName,
            string[] idColumns,
            string[] nameColumns,
            List<string> warnings)
        {
            Dictionary<string, string> values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (Dictionary<string, string> row in LoadTableRows(client, database, tables, tableName, warnings))
            {
                string id = GetValue(row, idColumns);
                string name = GetValue(row, nameColumns);
                if (!string.IsNullOrWhiteSpace(id) && !string.IsNullOrWhiteSpace(name))
                {
                    values[id] = name;
                }
            }
            return values;
        }

        private static List<Dictionary<string, string>> LoadTableRows(
            MysqlClient client,
            string database,
            Dictionary<string, List<ColumnInfo>> tables,
            string tableName,
            List<string> warnings)
        {
            List<Dictionary<string, string>> values = new List<Dictionary<string, string>>();
            if (!tables.ContainsKey(tableName))
            {
                warnings.Add("未找到佳鱼参数化设计表 " + tableName + "，已跳过。");
                return values;
            }

            List<ColumnInfo> selectedColumns = tables[tableName].Where(c => !IsBinaryType(c.DataType)).ToList();
            if (selectedColumns.Count == 0)
            {
                warnings.Add("佳鱼表 " + tableName + " 没有可读取的文本列，已跳过。");
                return values;
            }

            string selectList = string.Join(", ", selectedColumns.Select(c => QuoteIdentifier(c.Name)).ToArray());
            string sql = "SELECT " + selectList + " FROM " + QuoteIdentifier(database) + "." + QuoteIdentifier(tableName) +
                         " LIMIT " + RowLimitPerTable.ToString(System.Globalization.CultureInfo.InvariantCulture);
            List<string> columnNames;
            List<string[]> rows;
            try
            {
                rows = client.Query(sql, out columnNames);
            }
            catch (Exception ex)
            {
                warnings.Add("跳过表 " + tableName + ": " + ex.Message);
                return values;
            }

            if (rows.Count >= RowLimitPerTable)
            {
                warnings.Add("表 " + tableName + " 达到单表 " + RowLimitPerTable + " 行上限，后续可按增量主键细化同步。");
            }

            foreach (string[] row in rows)
            {
                Dictionary<string, string> item = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                for (int i = 0; i < columnNames.Count && i < row.Length; ++i)
                {
                    item[columnNames[i]] = row[i] ?? "";
                }
                values.Add(item);
            }
            return values;
        }

        private static List<CatalogNodeInfo> BuildCatalogPath(
            Dictionary<string, string> product,
            Dictionary<string, CatalogNodeInfo> catalogs,
            string productUrl)
        {
            List<CatalogNodeInfo> path = new List<CatalogNodeInfo>();
            HashSet<string> added = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (string id in SplitTreeNode(GetValue(product, "treeNode", "tree_node")))
            {
                if (catalogs.ContainsKey(id) && !added.Contains(id))
                {
                    path.Add(catalogs[id]);
                    added.Add(id);
                }
            }

            string catalogId = GetValue(product, "cid", "catalogId", "catalogID", "category_id");
            if (!string.IsNullOrWhiteSpace(catalogId) && catalogs.ContainsKey(catalogId) && !added.Contains(catalogId))
            {
                path = BuildCatalogChain(catalogId, catalogs);
            }

            if (path.Count == 0)
            {
                path = BuildCatalogPathFromUrl(productUrl);
            }
            return path;
        }

        private static List<CatalogNodeInfo> BuildCatalogChain(
            string catalogId,
            Dictionary<string, CatalogNodeInfo> catalogs)
        {
            List<CatalogNodeInfo> reversed = new List<CatalogNodeInfo>();
            HashSet<string> seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            string current = catalogId;
            while (!string.IsNullOrWhiteSpace(current) && catalogs.ContainsKey(current) && !seen.Contains(current))
            {
                CatalogNodeInfo node = catalogs[current];
                reversed.Add(node);
                seen.Add(current);
                current = node.ParentId;
                if (current == "0")
                {
                    break;
                }
            }
            reversed.Reverse();
            return reversed;
        }

        private static List<CatalogNodeInfo> BuildCatalogPathFromUrl(string productUrl)
        {
            List<CatalogNodeInfo> path = new List<CatalogNodeInfo>();
            List<string> parts = SplitPath(productUrl);
            if (parts.Count > 0 && LooksLikeProductModel(parts[parts.Count - 1], parts[parts.Count - 1]))
            {
                parts.RemoveAt(parts.Count - 1);
            }
            string fullPath = "";
            string parentId = "";
            foreach (string part in parts)
            {
                fullPath = CombinePath(fullPath, part);
                CatalogNodeInfo node = new CatalogNodeInfo();
                node.Id = "path:" + fullPath;
                node.ParentId = parentId;
                node.Name = part;
                node.FullPath = fullPath;
                path.Add(node);
                parentId = node.Id;
            }
            return path;
        }

        private static string BuildCatalogFullPath(
            CatalogNodeInfo node,
            Dictionary<string, CatalogNodeInfo> catalogs,
            int depth)
        {
            if (node == null)
            {
                return "";
            }
            if (depth > 30 || string.IsNullOrWhiteSpace(node.ParentId) || node.ParentId == "0" || !catalogs.ContainsKey(node.ParentId))
            {
                return node.Name;
            }
            string parent = BuildCatalogFullPath(catalogs[node.ParentId], catalogs, depth + 1);
            return CombinePath(parent, node.Name);
        }

        private static void AssignDocumentNames(List<RecordInfo> records)
        {
            Dictionary<string, int> counts = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
            foreach (RecordInfo record in records)
            {
                if (!counts.ContainsKey(record.Name))
                {
                    counts[record.Name] = 0;
                }
                ++counts[record.Name];
            }

            foreach (RecordInfo record in records)
            {
                string documentName = record.Name;
                if (counts.ContainsKey(record.Name) && counts[record.Name] > 1)
                {
                    string catalogPath = GetValue(record.Values, "catalog_path");
                    if (!string.IsNullOrWhiteSpace(catalogPath))
                    {
                        documentName = record.Name + " - " + catalogPath;
                    }
                }
                record.Values["document_name"] = Truncate(documentName, 180);
            }
        }

        private static void WriteKnowledgeBase(string path, string database, List<RecordInfo> records)
        {
            using (StreamWriter writer = new StreamWriter(path, false, Utf8NoBom))
            {
                foreach (RecordInfo record in records.OrderBy(r => r.Key))
                {
                    string content = BuildContent(record);
                    string documentName = GetValue(record.Values, "document_name");
                    if (string.IsNullOrWhiteSpace(documentName))
                    {
                        documentName = record.Name;
                    }
                    writer.WriteLine(
                        "{\"id\":\"" + JsonEscape(record.NodeId) + "\"," +
                        "\"source\":\"jiavu_mysql\"," +
                        "\"database\":\"" + JsonEscape(database) + "\"," +
                        "\"type\":\"" + JsonEscape(record.Type) + "\"," +
                        "\"name\":\"" + JsonEscape(documentName) + "\"," +
                        "\"content\":\"" + JsonEscape(content) + "\"," +
                        "\"metadata\":{\"row_key\":\"" + JsonEscape(record.Key) + "\"," +
                        "\"fingerprint\":\"" + record.Fingerprint + "\"," +
                        "\"values\":" + ValuesJson(record.Values) + "}}");
                }
            }
        }

        private static void WriteGraph(string path, string database, List<RecordInfo> records)
        {
            StringBuilder nodes = new StringBuilder();
            StringBuilder edges = new StringBuilder();
            string rootNode = "jiavu:parametric-design";
            HashSet<string> appendedNodes = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            HashSet<string> appendedEdges = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

            AppendNodeOnce(nodes, appendedNodes, rootNode, "SelectionRoot", "佳鱼参数化设计", "jiavu:parametric-design");
            foreach (RecordInfo record in records.OrderBy(r => r.Key))
            {
                string parentNode = rootNode;
                foreach (CatalogNodeInfo catalog in record.CatalogPath)
                {
                    string catalogNode = "catalog:" + HashHex(catalog.Id + "|" + catalog.FullPath).Substring(0, 24);
                    AppendNodeOnce(nodes, appendedNodes, catalogNode, "Catalog", catalog.Name, catalog.FullPath);
                    AppendEdgeOnce(
                        edges,
                        appendedEdges,
                        parentNode,
                        catalogNode,
                        parentNode == rootNode ? "HAS_SELECTION_CATEGORY" : "HAS_CHILD_CATEGORY");
                    parentNode = catalogNode;
                }

                AppendNodeOnce(nodes, appendedNodes, record.NodeId, record.Type, record.Name, record.Key,
                    BuildProductPropsJson(record));
                AppendEdgeOnce(edges, appendedEdges, parentNode, record.NodeId, "HAS_PRODUCT_MODEL");
            }

            File.WriteAllText(
                path,
                "{\"schema_version\":\"1.0\",\"source\":\"jiavu_mysql\",\"database\":\"" + JsonEscape(database) +
                    "\",\"generated_at\":\"" + DateTime.UtcNow.ToString("o") + "\",\"nodes\":[" + nodes +
                    "],\"edges\":[" + edges + "]}",
                Utf8NoBom);
        }

        private static void AppendNodeOnce(
            StringBuilder builder,
            HashSet<string> appended,
            string id,
            string type,
            string name,
            string key)
        {
            AppendNodeOnce(builder, appended, id, type, name, key, null);
        }

        private static void AppendNodeOnce(
            StringBuilder builder,
            HashSet<string> appended,
            string id,
            string type,
            string name,
            string key,
            string propsJson)
        {
            if (appended.Contains(id))
            {
                return;
            }
            appended.Add(id);
            AppendNode(builder, id, type, name, key, propsJson);
        }

        private static void AppendEdgeOnce(
            StringBuilder builder,
            HashSet<string> appended,
            string source,
            string target,
            string type)
        {
            string key = source + "|" + target + "|" + type;
            if (appended.Contains(key))
            {
                return;
            }
            appended.Add(key);
            AppendEdge(builder, source, target, type);
        }

        private static void AppendNode(StringBuilder builder, string id, string type, string name, string key)
        {
            AppendNode(builder, id, type, name, key, null);
        }

        private static void AppendNode(StringBuilder builder, string id, string type, string name, string key, string propsJson)
        {
            if (builder.Length > 0)
            {
                builder.Append(",");
            }
            builder.Append("{\"id\":\"").Append(JsonEscape(id))
                .Append("\",\"type\":\"").Append(JsonEscape(type))
                .Append("\",\"name\":\"").Append(JsonEscape(name))
                .Append("\",\"key\":\"").Append(JsonEscape(key));
            if (!string.IsNullOrEmpty(propsJson))
            {
                builder.Append("\",\"props\":").Append(propsJson);
            }
            else
            {
                builder.Append("\"");
            }
            builder.Append("}");
        }

        private static void AppendEdge(StringBuilder builder, string source, string target, string type)
        {
            if (builder.Length > 0)
            {
                builder.Append(",");
            }
            builder.Append("{\"source\":\"").Append(JsonEscape(source))
                .Append("\",\"target\":\"").Append(JsonEscape(target))
                .Append("\",\"type\":\"").Append(JsonEscape(type))
                .Append("\"}");
        }

        private static DifySyncResult SyncToDifyIfConfigured(
            JiavuDbConfig config,
            string knowledgeBasePath,
            string graphPath)
        {
            DifySyncResult result = new DifySyncResult();
            bool hasBase = !string.IsNullOrWhiteSpace(config.DifyBaseUrl);
            bool hasToken = !string.IsNullOrWhiteSpace(config.DifyDatasetApiKey);
            bool hasDataset = !string.IsNullOrWhiteSpace(config.DifyDatasetId);
            bool hasGraph = !string.IsNullOrWhiteSpace(config.DifyKnowledgeGraphId);
            if (!hasBase && !hasToken && !hasDataset && !hasGraph)
            {
                result.Ok = true;
                result.Skipped = true;
                return result;
            }
            if (!hasBase || !hasToken)
            {
                result.Error = "Dify 同步需要填写 dify_sync_base_url 和 dify_dataset_api_key。";
                return result;
            }

            if (hasDataset)
            {
                SyncKnowledgeBaseToDify(config, knowledgeBasePath, result);
            }
            if (hasGraph)
            {
                SyncKnowledgeGraphToDify(config, graphPath, result);
            }
            if (!hasDataset && !hasGraph)
            {
                result.Error = "Dify 同步需要至少填写 dify_dataset_id 或 dify_knowledge_graph_id。";
                return result;
            }
            result.Ok = string.IsNullOrWhiteSpace(result.Error);
            return result;
        }

        private static void SyncKnowledgeBaseToDify(JiavuDbConfig config, string knowledgeBasePath, DifySyncResult result)
        {
            DeleteDifyDocumentIfExists(config, "jiavu_knowledge_base.jsonl");

            int syncedCount = 0;
            HashSet<string> desiredNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            List<DifyDocumentEntry> entries = new List<DifyDocumentEntry>();
            foreach (string line in File.ReadLines(knowledgeBasePath, Utf8NoBom))
            {
                if (string.IsNullOrWhiteSpace(line))
                {
                    continue;
                }

                DifyDocumentEntry entry = ParseDifyDocumentEntry(config, line);
                if (string.IsNullOrWhiteSpace(entry.Name) || string.IsNullOrWhiteSpace(entry.Text))
                {
                    continue;
                }
                entries.Add(entry);
                desiredNames.Add(entry.Name);
            }

            DeleteStaleDifyDocuments(config, desiredNames);

            foreach (DifyDocumentEntry entry in entries)
            {
                string documentId = FindDifyDocumentId(config, entry.Name);
                string endpoint = documentId.Length > 0
                    ? "/datasets/" + Uri.EscapeDataString(config.DifyDatasetId) + "/documents/" +
                        Uri.EscapeDataString(documentId) + "/update-by-text"
                    : "/datasets/" + Uri.EscapeDataString(config.DifyDatasetId) + "/document/create-by-text";
                string payload =
                    "{\"name\":\"" + JsonEscape(entry.Name) + "\"," +
                    "\"text\":\"" + JsonEscape(entry.Text) + "\"," +
                    "\"doc_form\":\"text_model\"," +
                    "\"doc_language\":\"Chinese\"," +
                    "\"indexing_technique\":\"" + JsonEscape(config.DifyDatasetIndexingTechnique) + "\"," +
                    "\"process_rule\":{\"mode\":\"custom\",\"rules\":{\"pre_processing_rules\":[" +
                    "{\"id\":\"remove_extra_spaces\",\"enabled\":true}," +
                    "{\"id\":\"remove_urls_emails\",\"enabled\":false}]," +
                    "\"segmentation\":{\"separator\":\"\\n\",\"max_tokens\":800,\"chunk_overlap\":0}}}}";
                HttpResult response = PostDifyJson(config, endpoint, payload);
                result.KnowledgeBaseStatusCode = response.StatusCode;
                if (!response.Ok)
                {
                    result.Error = "Dify 知识库文档同步失败（" + entry.Name + "），HTTP " +
                        response.StatusCode + "：" + response.Body;
                    return;
                }
                ++syncedCount;
            }

            result.KnowledgeBaseDocumentCount = syncedCount;
            result.KnowledgeBaseSynced = true;
        }

        private static void DeleteStaleDifyDocuments(JiavuDbConfig config, HashSet<string> desiredNames)
        {
            string prefix = NormalizeKnowledgeBaseDocumentPrefix(config.DifyKnowledgeBaseDocumentPrefix) + " - ";
            List<DifyDocumentRef> documents = ListDifyDocuments(config);
            foreach (DifyDocumentRef document in documents)
            {
                if (!document.Name.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }
                if (desiredNames.Contains(document.Name))
                {
                    continue;
                }
                string endpoint = "/datasets/" + Uri.EscapeDataString(config.DifyDatasetId) + "/documents/" +
                    Uri.EscapeDataString(document.Id);
                SendDifyRequest(config, "DELETE", endpoint, "");
            }
        }

        private static List<DifyDocumentRef> ListDifyDocuments(JiavuDbConfig config)
        {
            List<DifyDocumentRef> documents = new List<DifyDocumentRef>();
            for (int page = 1; page <= 100; ++page)
            {
                string endpoint = "/datasets/" + Uri.EscapeDataString(config.DifyDatasetId) +
                    "/documents?page=" + page.ToString(System.Globalization.CultureInfo.InvariantCulture) + "&limit=100";
                HttpResult response = SendDifyRequest(config, "GET", endpoint, "");
                if (!response.Ok)
                {
                    break;
                }

                List<DifyDocumentRef> pageDocuments = ParseDifyDocumentRefs(response.Body);
                if (pageDocuments.Count == 0)
                {
                    break;
                }
                documents.AddRange(pageDocuments);
                if (pageDocuments.Count < 100)
                {
                    break;
                }
            }
            return documents;
        }

        private static List<DifyDocumentRef> ParseDifyDocumentRefs(string json)
        {
            List<DifyDocumentRef> documents = new List<DifyDocumentRef>();
            MatchCollection matches = Regex.Matches(
                json,
                "\"id\"\\s*:\\s*\"(?<id>[0-9a-fA-F-]{36})\"[\\s\\S]{0,1200}?\"name\"\\s*:\\s*\"(?<name>(?:\\\\.|[^\"])*)\"");
            foreach (Match match in matches)
            {
                DifyDocumentRef document = new DifyDocumentRef();
                document.Id = match.Groups["id"].Value;
                document.Name = JsonUnescape(match.Groups["name"].Value);
                documents.Add(document);
            }
            return documents;
        }

        private static void SyncKnowledgeGraphToDify(JiavuDbConfig config, string graphPath, DifySyncResult result)
        {
            string graphJson = File.ReadAllText(graphPath, Utf8NoBom);
            string payload = InsertJsonBooleanProperty(graphJson, "clear_existing", true);
            string endpoint = "/knowledge-graphs/" + Uri.EscapeDataString(config.DifyKnowledgeGraphId) + "/sync";
            HttpResult response = PostDifyJson(config, endpoint, payload);
            result.KnowledgeGraphStatusCode = response.StatusCode;
            if (!response.Ok)
            {
                result.Error = "Dify 知识图谱同步失败，HTTP " + response.StatusCode + "：" + response.Body;
                return;
            }
            result.KnowledgeGraphSynced = true;
        }

        private static string FindDifyDocumentId(JiavuDbConfig config, string name)
        {
            string endpoint = "/datasets/" + Uri.EscapeDataString(config.DifyDatasetId) +
                "/documents?limit=100&keyword=" + Uri.EscapeDataString(name);
            HttpResult response = SendDifyRequest(config, "GET", endpoint, "");
            if (!response.Ok)
            {
                return "";
            }

            MatchCollection matches = Regex.Matches(
                response.Body,
                "\"id\"\\s*:\\s*\"(?<id>[0-9a-fA-F-]{36})\"[\\s\\S]{0,800}?\"name\"\\s*:\\s*\"" +
                    Regex.Escape(JsonEscape(name)) + "\"");
            if (matches.Count > 0)
            {
                return matches[0].Groups["id"].Value;
            }
            matches = Regex.Matches(
                response.Body,
                "\"name\"\\s*:\\s*\"" + Regex.Escape(JsonEscape(name)) +
                    "\"[\\s\\S]{0,800}?\"id\"\\s*:\\s*\"(?<id>[0-9a-fA-F-]{36})\"");
            return matches.Count > 0 ? matches[0].Groups["id"].Value : "";
        }

        private static void DeleteDifyDocumentIfExists(JiavuDbConfig config, string name)
        {
            string documentId = FindDifyDocumentId(config, name);
            if (documentId.Length == 0)
            {
                return;
            }
            string endpoint = "/datasets/" + Uri.EscapeDataString(config.DifyDatasetId) + "/documents/" +
                Uri.EscapeDataString(documentId);
            SendDifyRequest(config, "DELETE", endpoint, "");
        }

        private static DifyDocumentEntry ParseDifyDocumentEntry(JiavuDbConfig config, string json)
        {
            DifyDocumentEntry entry = new DifyDocumentEntry();
            string name = JsonValue(json, "name").Trim();
            string content = JsonValue(json, "content").Trim();
            string prefix = NormalizeKnowledgeBaseDocumentPrefix(config.DifyKnowledgeBaseDocumentPrefix);
            if (string.IsNullOrWhiteSpace(name))
            {
                name = JsonValue(json, "id").Trim();
            }
            entry.Name = string.IsNullOrWhiteSpace(name) ? "" : prefix + " - " + name;
            entry.Text = content;
            return entry;
        }

        private static string NormalizeKnowledgeBaseDocumentPrefix(string value)
        {
            string prefix = (value ?? "").Trim();
            if (prefix.Length == 0 ||
                string.Equals(prefix, "jiavu_knowledge_base.jsonl", StringComparison.OrdinalIgnoreCase))
            {
                // Unicode escape avoids .NET Framework csc.exe ANSI fallback on BOMless source.
                // Literal Chinese here gets Latin-1 mojibaked into garbage when stored in Dify PG.
                return "\u9759\u6001\u914d\u7f6e";
            }
            return prefix;
        }

        private static HttpResult PostDifyJson(JiavuDbConfig config, string endpoint, string payload)
        {
            return SendDifyRequest(config, "POST", endpoint, payload);
        }

        private static void EnrichFromJyasmService(
            JiavuDbConfig config,
            List<RecordInfo> records,
            List<string> warnings)
        {
            if (string.IsNullOrWhiteSpace(config.JyasmServiceBaseUrl))
            {
                return;
            }

            string serviceBase = NormalizeBaseUrl(config.JyasmServiceBaseUrl);
            string publicBase = !string.IsNullOrWhiteSpace(config.JyasmServicePublicBaseUrl)
                ? NormalizeBaseUrl(config.JyasmServicePublicBaseUrl)
                : serviceBase;

            if (config.JyasmServiceTriggerRefresh)
            {
                try
                {
                    // /refresh 现在是异步的：立刻返回 job_id，真正的解密在 jyasm-service
                    // 后台线程里跑。要轮询 /refresh/status，等到 status != "running" 才能继续，
                    // 否则后面拉 manifest 会撞上一堆 404（产品还没解出来）。
                    HttpResult refreshResult = SendJyasmRequest(config, "POST", "refresh", null);
                    if (!refreshResult.Ok)
                    {
                        warnings.Add("佳鱼解密服务 /refresh 失败：HTTP " + refreshResult.StatusCode +
                            " " + Truncate(refreshResult.Body ?? "", 200));
                    }
                    else
                    {
                        WaitForJyasmRefresh(config, warnings);
                    }
                }
                catch (Exception ex)
                {
                    warnings.Add("佳鱼解密服务 /refresh 异常：" + ex.Message);
                }
            }

            JavaScriptSerializer ser = new JavaScriptSerializer();
            ser.MaxJsonLength = int.MaxValue;
            int enriched = 0;
            int missing = 0;

            foreach (RecordInfo record in records)
            {
                string productId = GetValue(record.Values, "product_id");
                if (string.IsNullOrWhiteSpace(productId))
                {
                    continue;
                }

                HttpResult manifestResult;
                try
                {
                    manifestResult = SendJyasmRequest(config, "GET",
                        "products/" + Uri.EscapeDataString(productId) + "/manifest", null);
                }
                catch (Exception ex)
                {
                    warnings.Add("佳鱼解密服务 manifest 请求异常 productId=" + productId + "：" + ex.Message);
                    continue;
                }
                if (!manifestResult.Ok)
                {
                    if (manifestResult.StatusCode == 404)
                    {
                        missing++;
                    }
                    else
                    {
                        warnings.Add("佳鱼解密服务 manifest 失败 productId=" + productId +
                            " HTTP " + manifestResult.StatusCode);
                    }
                    continue;
                }

                Dictionary<string, object> manifest = null;
                try
                {
                    manifest = ser.Deserialize<Dictionary<string, object>>(manifestResult.Body ?? "");
                }
                catch (Exception ex)
                {
                    warnings.Add("佳鱼解密服务 manifest 解析失败 productId=" + productId + "：" + ex.Message);
                    continue;
                }
                if (manifest == null)
                {
                    continue;
                }

                record.JyasmSourcePath = AsString(manifest, "source_jyasm");
                record.JyasmExtractedDir = AsString(manifest, "extracted_dir");
                if (!string.IsNullOrEmpty(record.JyasmSourcePath))
                {
                    record.Values["jyasm_source_path"] = record.JyasmSourcePath;
                }
                if (!string.IsNullOrEmpty(record.JyasmExtractedDir))
                {
                    record.Values["jyasm_extracted_dir"] = record.JyasmExtractedDir;
                }

                System.Collections.ArrayList imgs = ManifestArray(manifest, "images");
                if (imgs != null)
                {
                    foreach (object item in imgs)
                    {
                        Dictionary<string, object> img = item as Dictionary<string, object>;
                        if (img == null)
                        {
                            continue;
                        }
                        string url = AsString(img, "url");
                        if (string.IsNullOrEmpty(url))
                        {
                            continue;
                        }
                        string full = publicBase + url.TrimStart('/');
                        record.JyasmImageUrls.Add(full);
                    }
                }
                if (record.JyasmImageUrls.Count > 0)
                {
                    record.Values["jyasm_image_urls"] = string.Join(";", record.JyasmImageUrls.ToArray());
                }

                System.Collections.ArrayList paramArr = ManifestArray(manifest, "params");
                if (paramArr != null)
                {
                    Dictionary<string, ProductParameterInfo> byName =
                        new Dictionary<string, ProductParameterInfo>(StringComparer.OrdinalIgnoreCase);
                    foreach (ProductParameterInfo p in record.Parameters)
                    {
                        string nk = StripUnit(p.Name);
                        if (!string.IsNullOrEmpty(nk) && !byName.ContainsKey(nk))
                        {
                            byName[nk] = p;
                        }
                    }
                    int nextSort = record.Parameters.Count;
                    bool addedAny = false;
                    foreach (object item in paramArr)
                    {
                        Dictionary<string, object> pd = item as Dictionary<string, object>;
                        if (pd == null)
                        {
                            continue;
                        }
                        // 派生参数（由公式算出，jyasm 不要求用户填）跳过，避免污染参数模板。
                        string derivedRaw = AsString(pd, "is_derived");
                        if (string.Equals(derivedRaw, "True", StringComparison.OrdinalIgnoreCase) ||
                            string.Equals(derivedRaw, "true", StringComparison.OrdinalIgnoreCase))
                        {
                            continue;
                        }
                        // 静态隐藏（type_expr == "Hidden"）的参数完全跳过 —— 既不进 record.Parameters，
                        // 也不出现在知识库文档里，避免污染 LLM 检索结果。
                        // 条件可见（{$IF$...$ENDIF$}）会被保留并打上 visibility=conditional 标签，
                        // 前端会显示但加 "条件显示" tag。
                        string visibility = AsString(pd, "visibility");
                        if (string.Equals(visibility, "hidden", StringComparison.OrdinalIgnoreCase))
                        {
                            continue;
                        }
                        string nm = AsString(pd, "name");
                        string nmKey = StripUnit(nm);
                        if (string.IsNullOrEmpty(nmKey))
                        {
                            continue;
                        }
                        ProductParameterInfo target;
                        if (!byName.TryGetValue(nmKey, out target))
                        {
                            // jyasm 解出来但 MySQL product_parameters 表里没有的参数，
                            // 直接以 jyasm 为准追加 —— 这里是修复 "参数模板为空" 的关键。
                            target = new ProductParameterInfo();
                            target.Name = nm;
                            target.Sort = nextSort++;
                            record.Parameters.Add(target);
                            byName[nmKey] = target;
                            addedAny = true;
                        }
                        if (string.IsNullOrEmpty(target.Unit))
                        {
                            target.Unit = AsString(pd, "unit");
                        }
                        if (string.IsNullOrEmpty(target.Range))
                        {
                            target.Range = AsString(pd, "range");
                        }
                        if (string.IsNullOrEmpty(target.Remark))
                        {
                            target.Remark = AsString(pd, "remark");
                        }
                        if (string.IsNullOrEmpty(target.Default))
                        {
                            target.Default = AsString(pd, "default");
                        }
                        if (target.StepPage == 0)
                        {
                            int sp;
                            if (int.TryParse(AsString(pd, "step_page"), out sp))
                            {
                                target.StepPage = sp;
                            }
                        }
                        if (string.IsNullOrEmpty(target.Image))
                        {
                            target.Image = AsString(pd, "image");
                        }
                        if (string.IsNullOrEmpty(target.ImageUrl))
                        {
                            string imgUrl = AsString(pd, "image_url");
                            if (!string.IsNullOrEmpty(imgUrl))
                            {
                                target.ImageUrl = publicBase + imgUrl.TrimStart('/');
                            }
                        }
                        // visibility 永远以 jyasm 为准（MySQL 没这个字段）；只在非空时写入，
                        // 保留 always 默认值供前端兜底。
                        if (!string.IsNullOrEmpty(visibility))
                        {
                            target.Visibility = visibility;
                        }
                        if (string.IsNullOrEmpty(target.TypeExpr))
                        {
                            target.TypeExpr = AsString(pd, "type");
                        }
                    }
                    if (addedAny)
                    {
                        // record.Values 里的 parameter_count / parameter_names 是
                        // LoadParameterizedDesignModels 阶段从 MySQL 计算的，新增 jyasm 参数后需要同步刷新，
                        // 否则知识库文档头部说"共 0 项参数" 与正文不一致，影响 Dify 检索质量。
                        record.Values["parameter_count"] = record.Parameters.Count
                            .ToString(System.Globalization.CultureInfo.InvariantCulture);
                        record.Values["parameter_names"] = string.Join("；",
                            record.Parameters.Select(p => p.Name).ToArray());
                    }
                }

                System.Collections.ArrayList gbArr = ManifestArray(manifest, "gb_standard_parts");
                if (gbArr != null)
                {
                    foreach (object item in gbArr)
                    {
                        Dictionary<string, object> gb = item as Dictionary<string, object>;
                        if (gb == null)
                        {
                            continue;
                        }
                        GbStandardPartInfo info = new GbStandardPartInfo();
                        info.Gb = AsString(gb, "gb");
                        info.Meaning = AsString(gb, "meaning");
                        info.Spec = AsString(gb, "spec");
                        info.Material = AsString(gb, "material");
                        if (!string.IsNullOrEmpty(info.Gb))
                        {
                            record.GbParts.Add(info);
                        }
                    }
                }

                record.JyasmEnriched = true;
                enriched++;
            }

            if (enriched > 0)
            {
                warnings.Add("佳鱼解密服务已补充 " + enriched + " 个产品的参数说明/图片/标准件。");
            }
            if (missing > 0)
            {
                warnings.Add("佳鱼解密服务缺少 " + missing + " 个 productId 的 manifest（可能源 .jyasm 未上传或解密失败）。");
            }
        }

        // 总结：轮询 /refresh/status 直到后台 job 完成；最长等 60 分钟，超时记 warning 后让上层继续。
        private static void WaitForJyasmRefresh(JiavuDbConfig config, List<string> warnings)
        {
            JavaScriptSerializer ser = new JavaScriptSerializer();
            ser.MaxJsonLength = int.MaxValue;
            const int maxWaitMs = 60 * 60 * 1000;   // 60 分钟硬上限
            const int pollIntervalMs = 3000;
            int waited = 0;
            int consecutiveErrors = 0;
            while (waited < maxWaitMs)
            {
                HttpResult statusResult;
                try
                {
                    statusResult = SendJyasmRequest(config, "GET", "refresh/status", null);
                }
                catch (Exception ex)
                {
                    consecutiveErrors++;
                    if (consecutiveErrors >= 5)
                    {
                        warnings.Add("佳鱼解密服务 /refresh/status 连续失败：" + ex.Message);
                        return;
                    }
                    Thread.Sleep(pollIntervalMs);
                    waited += pollIntervalMs;
                    continue;
                }
                if (!statusResult.Ok)
                {
                    consecutiveErrors++;
                    if (consecutiveErrors >= 5)
                    {
                        warnings.Add("佳鱼解密服务 /refresh/status 持续返回 HTTP " + statusResult.StatusCode);
                        return;
                    }
                    Thread.Sleep(pollIntervalMs);
                    waited += pollIntervalMs;
                    continue;
                }
                consecutiveErrors = 0;
                Dictionary<string, object> snap;
                try
                {
                    snap = ser.Deserialize<Dictionary<string, object>>(statusResult.Body ?? "");
                }
                catch (Exception ex)
                {
                    warnings.Add("佳鱼解密服务 /refresh/status 响应解析失败：" + ex.Message);
                    return;
                }
                string status = AsString(snap, "status");
                if (string.IsNullOrEmpty(status) || status == "idle" || status == "completed")
                {
                    return;
                }
                if (status == "error")
                {
                    warnings.Add("佳鱼解密服务 /refresh job 失败：" + Truncate(AsString(snap, "error"), 200));
                    return;
                }
                // 仍在 running，继续等
                Thread.Sleep(pollIntervalMs);
                waited += pollIntervalMs;
            }
            warnings.Add("佳鱼解密服务 /refresh 超过 60 分钟仍未完成，已放弃等待。后续 manifest 可能为空。");
        }

        private static HttpResult SendJyasmRequest(JiavuDbConfig config, string method, string endpoint, string payload)
        {
            string url = NormalizeBaseUrl(config.JyasmServiceBaseUrl) + endpoint.TrimStart('/');
            HttpWebRequest request = (HttpWebRequest)WebRequest.Create(url);
            request.Method = method;
            int timeout = config.JyasmServiceTimeoutMs > 0 ? config.JyasmServiceTimeoutMs : 900000;
            request.Timeout = timeout;
            request.ReadWriteTimeout = timeout;
            request.Accept = "application/json";
            if (!string.IsNullOrWhiteSpace(config.JyasmServiceToken))
            {
                request.Headers[HttpRequestHeader.Authorization] = "Bearer " + config.JyasmServiceToken;
            }
            if (!string.Equals(method, "GET", StringComparison.OrdinalIgnoreCase))
            {
                byte[] body = Encoding.UTF8.GetBytes(payload ?? "");
                request.ContentType = "application/json; charset=utf-8";
                request.ContentLength = body.Length;
                if (body.Length > 0)
                {
                    using (Stream stream = request.GetRequestStream())
                    {
                        stream.Write(body, 0, body.Length);
                    }
                }
            }

            try
            {
                using (HttpWebResponse response = (HttpWebResponse)request.GetResponse())
                using (Stream stream = response.GetResponseStream())
                using (StreamReader reader = new StreamReader(stream, Encoding.UTF8))
                {
                    return new HttpResult
                    {
                        Ok = (int)response.StatusCode >= 200 && (int)response.StatusCode < 300,
                        StatusCode = (int)response.StatusCode,
                        Body = reader.ReadToEnd()
                    };
                }
            }
            catch (WebException ex)
            {
                HttpWebResponse response = ex.Response as HttpWebResponse;
                if (response != null)
                {
                    string body = "";
                    using (Stream stream = response.GetResponseStream())
                    using (StreamReader reader = new StreamReader(stream, Encoding.UTF8))
                    {
                        body = reader.ReadToEnd();
                    }
                    return new HttpResult { Ok = false, StatusCode = (int)response.StatusCode, Body = body };
                }
                return new HttpResult { Ok = false, StatusCode = 0, Body = ex.Message };
            }
        }

        private static string AsString(Dictionary<string, object> dict, string key)
        {
            object value;
            if (dict == null || !dict.TryGetValue(key, out value) || value == null)
            {
                return "";
            }
            return value.ToString();
        }

        private static System.Collections.ArrayList ManifestArray(Dictionary<string, object> dict, string key)
        {
            object value;
            if (dict == null || !dict.TryGetValue(key, out value))
            {
                return null;
            }
            return value as System.Collections.ArrayList;
        }

        private static string StripUnit(string name)
        {
            if (string.IsNullOrEmpty(name))
            {
                return "";
            }
            int idx = name.IndexOf('[');
            if (idx > 0)
            {
                return name.Substring(0, idx).Trim();
            }
            return name.Trim();
        }

        private static HttpResult SendDifyRequest(JiavuDbConfig config, string method, string endpoint, string payload)
        {
            string url = NormalizeBaseUrl(config.DifyBaseUrl) + endpoint.TrimStart('/');
            HttpWebRequest request = (HttpWebRequest)WebRequest.Create(url);
            request.Method = method;
            request.Timeout = 900000;
            request.ReadWriteTimeout = 900000;
            request.Accept = "application/json";
            request.Headers[HttpRequestHeader.Authorization] = "Bearer " + config.DifyDatasetApiKey;
            if (!string.Equals(method, "GET", StringComparison.OrdinalIgnoreCase))
            {
                byte[] body = Encoding.UTF8.GetBytes(payload ?? "");
                request.ContentType = "application/json; charset=utf-8";
                request.ContentLength = body.Length;
                using (Stream stream = request.GetRequestStream())
                {
                    stream.Write(body, 0, body.Length);
                }
            }

            try
            {
                using (HttpWebResponse response = (HttpWebResponse)request.GetResponse())
                using (Stream stream = response.GetResponseStream())
                using (StreamReader reader = new StreamReader(stream, Encoding.UTF8))
                {
                    return new HttpResult
                    {
                        Ok = (int)response.StatusCode >= 200 && (int)response.StatusCode < 300,
                        StatusCode = (int)response.StatusCode,
                        Body = reader.ReadToEnd()
                    };
                }
            }
            catch (WebException ex)
            {
                HttpWebResponse response = ex.Response as HttpWebResponse;
                string body = "";
                if (response != null)
                {
                    using (Stream stream = response.GetResponseStream())
                    using (StreamReader reader = new StreamReader(stream, Encoding.UTF8))
                    {
                        body = reader.ReadToEnd();
                    }
                    return new HttpResult { Ok = false, StatusCode = (int)response.StatusCode, Body = body };
                }
                return new HttpResult { Ok = false, StatusCode = 0, Body = ex.Message };
            }
        }

        private static string NormalizeBaseUrl(string value)
        {
            string url = (value ?? "").Trim();
            if (url.Length == 0)
            {
                return "";
            }
            if (!url.EndsWith("/", StringComparison.Ordinal))
            {
                url += "/";
            }
            return url;
        }

        private static string InsertJsonBooleanProperty(string json, string name, bool value)
        {
            string text = (json ?? "").Trim();
            if (text.Length < 2 || text[0] != '{')
            {
                return json;
            }
            if (Regex.IsMatch(text, "\"" + Regex.Escape(name) + "\"\\s*:"))
            {
                return text;
            }
            return "{\"" + JsonEscape(name) + "\":" + (value ? "true" : "false") + "," + text.Substring(1);
        }

        private static string BuildReportMessage(int recordCount, DeltaInfo delta, DifySyncResult difySync)
        {
            StringBuilder message = new StringBuilder();
            message.Append("知识图谱更新完成：知识库 ").Append(recordCount)
                .Append(" 条，新增 ").Append(delta.Added)
                .Append("，更新 ").Append(delta.Updated)
                .Append("，删除 ").Append(delta.Removed).Append("。");
            if (difySync.Skipped)
            {
                message.Append(" 未配置 Dify 同步，已仅生成本地文件。");
            }
            else if (difySync.Ok)
            {
                if (difySync.KnowledgeBaseSynced)
                {
                    message.Append(" Dify 知识库已同步 ")
                        .Append(difySync.KnowledgeBaseDocumentCount)
                        .Append(" 个文档。");
                }
                if (difySync.KnowledgeGraphSynced)
                {
                    message.Append(" Dify 知识图谱已同步。");
                }
            }
            else if (!string.IsNullOrWhiteSpace(difySync.Error))
            {
                message.Append(" ").Append(difySync.Error);
            }
            return message.ToString();
        }

        private static void WriteManifest(string path, JiavuDbConfig config, string database, List<RecordInfo> records)
        {
            StringBuilder entries = new StringBuilder();
            foreach (RecordInfo record in records.OrderBy(r => r.Key))
            {
                if (entries.Length > 0)
                {
                    entries.Append(",");
                }
                entries.Append("{\"key\":\"").Append(JsonEscape(record.Key))
                    .Append("\",\"fingerprint\":\"").Append(record.Fingerprint)
                    .Append("\",\"table\":\"").Append(JsonEscape(record.TableName))
                    .Append("\",\"type\":\"").Append(JsonEscape(record.Type))
                    .Append("\"}");
            }

            string json =
                "{\"schema_version\":\"1.0\",\"source\":\"jiavu_mysql\",\"host\":\"" + JsonEscape(config.Host) +
                "\",\"port\":" + config.Port.ToString(System.Globalization.CultureInfo.InvariantCulture) +
                ",\"database\":\"" + JsonEscape(database) +
                "\",\"user\":\"" + JsonEscape(config.User) +
                "\",\"generated_at\":\"" + DateTime.UtcNow.ToString("o") +
                "\",\"entry_count\":" + records.Count.ToString(System.Globalization.CultureInfo.InvariantCulture) +
                ",\"entries\":[" + entries + "]}";
            File.WriteAllText(path, json, Utf8NoBom);
        }

        private static void WriteDelta(string path, string database, DeltaInfo delta)
        {
            string json =
                "{\"schema_version\":\"1.0\",\"source\":\"jiavu_mysql\",\"database\":\"" + JsonEscape(database) +
                "\",\"generated_at\":\"" + DateTime.UtcNow.ToString("o") +
                "\",\"added\":" + delta.Added +
                ",\"updated\":" + delta.Updated +
                ",\"removed\":" + delta.Removed +
                ",\"unchanged\":" + delta.Unchanged +
                ",\"added_keys\":" + StringArrayJson(delta.AddedKeys) +
                ",\"updated_keys\":" + StringArrayJson(delta.UpdatedKeys) +
                ",\"removed_keys\":" + StringArrayJson(delta.RemovedKeys) + "}";
            File.WriteAllText(path, json, Utf8NoBom);
        }

        private static DeltaInfo BuildDelta(List<RecordInfo> records, Dictionary<string, string> oldFingerprints)
        {
            DeltaInfo delta = new DeltaInfo();
            Dictionary<string, string> current = records.ToDictionary(r => r.Key, r => r.Fingerprint, StringComparer.OrdinalIgnoreCase);
            foreach (KeyValuePair<string, string> item in current)
            {
                if (!oldFingerprints.ContainsKey(item.Key))
                {
                    ++delta.Added;
                    AddLimited(delta.AddedKeys, item.Key);
                }
                else if (!string.Equals(oldFingerprints[item.Key], item.Value, StringComparison.OrdinalIgnoreCase))
                {
                    ++delta.Updated;
                    AddLimited(delta.UpdatedKeys, item.Key);
                }
                else
                {
                    ++delta.Unchanged;
                }
            }
            foreach (string key in oldFingerprints.Keys)
            {
                if (!current.ContainsKey(key))
                {
                    ++delta.Removed;
                    AddLimited(delta.RemovedKeys, key);
                }
            }
            return delta;
        }

        private static void AddLimited(List<string> values, string value)
        {
            if (values.Count < 500)
            {
                values.Add(value);
            }
        }

        private static Dictionary<string, string> ReadManifestFingerprints(string path)
        {
            Dictionary<string, string> values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            if (!File.Exists(path))
            {
                return values;
            }
            string content = File.ReadAllText(path, Utf8NoBom);
            MatchCollection matches = Regex.Matches(
                content,
                "\\{\"key\":\"(?<key>(?:\\\\.|[^\"])*)\",\"fingerprint\":\"(?<fp>[a-fA-F0-9]+)\"");
            foreach (Match match in matches)
            {
                values[JsonUnescape(match.Groups["key"].Value)] = match.Groups["fp"].Value;
            }
            return values;
        }

        private static string BuildContent(RecordInfo record)
        {
            StringBuilder text = new StringBuilder();
            text.Append("佳鱼参数化设计模型").AppendLine();
            text.Append("模型名称: ").Append(record.Name).AppendLine();
            text.Append("template_name: ").Append(record.Name).AppendLine();
            string fullNodePath = GetValue(record.Values, "full_node_path");
            string catalogPath = GetValue(record.Values, "catalog_path");
            if (!string.IsNullOrWhiteSpace(fullNodePath))
            {
                text.Append("完整节点路径: ").Append(fullNodePath).AppendLine();
            }
            if (!string.IsNullOrWhiteSpace(catalogPath))
            {
                text.Append("所属目录: ").Append(catalogPath).AppendLine();
            }
            text.Append("节点类型: ProductModel").AppendLine();
            text.Append("产品ID: ").Append(GetValue(record.Values, "product_id")).AppendLine();
            if (record.Parameters.Count == 0)
            {
                text.Append("参数列表: 暂无参数定义").AppendLine();
            }
            else
            {
                List<ProductParameterInfo> ordered = record.Parameters
                    .OrderBy(p => p.StepPage == 0 ? int.MaxValue : p.StepPage)
                    .ThenBy(p => p.Sort)
                    .ThenBy(p => p.Name)
                    .ToList();
                text.Append("参数列表:").AppendLine();
                foreach (ProductParameterInfo parameter in ordered)
                {
                    text.Append("- ").Append(parameter.Name);
                    if (!string.IsNullOrWhiteSpace(parameter.Unit))
                    {
                        text.Append(" [").Append(parameter.Unit).Append("]");
                    }
                    if (!string.IsNullOrWhiteSpace(parameter.FieldName))
                    {
                        text.Append(" (字段: ").Append(parameter.FieldName).Append(")");
                    }
                    if (!string.IsNullOrWhiteSpace(parameter.Default))
                    {
                        text.Append(" 默认: ").Append(parameter.Default);
                    }
                    if (!string.IsNullOrWhiteSpace(parameter.Range))
                    {
                        text.Append(" 范围: ").Append(parameter.Range);
                    }
                    if (!string.IsNullOrWhiteSpace(parameter.Remark))
                    {
                        text.Append(" 说明: ").Append(parameter.Remark);
                    }
                    if (string.Equals(parameter.Visibility, "conditional", StringComparison.OrdinalIgnoreCase))
                    {
                        text.Append(" 可见性: 条件显示");
                    }
                    if (!string.IsNullOrWhiteSpace(parameter.ImageUrl))
                    {
                        text.Append(" 图片: ").Append(parameter.ImageUrl);
                    }
                    else if (!string.IsNullOrWhiteSpace(parameter.Image))
                    {
                        text.Append(" 图片: ").Append(parameter.Image);
                    }
                    text.AppendLine();
                }
                // 注：旧版本这里追加过 ```json``` 结构化块给 Dify Code 节点提取，
                // 但 Dify 默认 800 字符硬切分段把 JSON 切到字符串中间，正则跨 segment
                // 一次也匹配不到。现在改成前端拿到 selected_model 后直接调
                // jyasm-service /products + /manifest 拿结构化参数，KB 文档不再嵌 JSON。
            }
            if (record.GbParts.Count > 0)
            {
                text.AppendLine();
                text.Append("国标标准件:").AppendLine();
                foreach (GbStandardPartInfo gb in record.GbParts
                    .OrderBy(g => g.Gb, StringComparer.OrdinalIgnoreCase)
                    .ThenBy(g => g.Spec, StringComparer.OrdinalIgnoreCase))
                {
                    text.Append("- ").Append(gb.Gb);
                    if (!string.IsNullOrWhiteSpace(gb.Meaning))
                    {
                        text.Append("（").Append(gb.Meaning).Append("）");
                    }
                    if (!string.IsNullOrWhiteSpace(gb.Spec))
                    {
                        text.Append(" 规格: ").Append(gb.Spec);
                    }
                    if (!string.IsNullOrWhiteSpace(gb.Material))
                    {
                        text.Append(" 材质: ").Append(gb.Material);
                    }
                    text.AppendLine();
                }
            }
            if (!string.IsNullOrWhiteSpace(record.JyasmExtractedDir))
            {
                text.AppendLine();
                text.Append("解密产物目录: ").Append(record.JyasmExtractedDir).AppendLine();
            }
            if (record.JyasmImageUrls.Count > 0)
            {
                text.Append("参考图片:").AppendLine();
                foreach (string url in record.JyasmImageUrls)
                {
                    text.Append("- ").Append(url).AppendLine();
                }
            }
            return text.ToString();
        }

        private static string ValuesJson(Dictionary<string, string> values)
        {
            StringBuilder builder = new StringBuilder();
            builder.Append("{");
            bool first = true;
            foreach (KeyValuePair<string, string> item in values.OrderBy(v => v.Key))
            {
                if (!first)
                {
                    builder.Append(",");
                }
                builder.Append("\"").Append(JsonEscape(item.Key)).Append("\":\"")
                    .Append(JsonEscape(Truncate(item.Value, 800))).Append("\"");
                first = false;
            }
            builder.Append("}");
            return builder.ToString();
        }

        private static string StringArrayJson(List<string> values)
        {
            return "[" + string.Join(",", values.Select(v => "\"" + JsonEscape(v) + "\"").ToArray()) + "]";
        }

        private static string BuildProductPropsJson(RecordInfo record)
        {
            StringBuilder sb = new StringBuilder();
            sb.Append("{");
            bool first = true;
            string productId = GetValue(record.Values, "product_id");
            if (!string.IsNullOrEmpty(productId))
            {
                sb.Append("\"product_id\":\"").Append(JsonEscape(productId)).Append("\"");
                first = false;
            }
            if (!string.IsNullOrEmpty(record.JyasmSourcePath))
            {
                if (!first) sb.Append(",");
                sb.Append("\"jyasm_source_path\":\"").Append(JsonEscape(record.JyasmSourcePath)).Append("\"");
                first = false;
            }
            if (!string.IsNullOrEmpty(record.JyasmExtractedDir))
            {
                if (!first) sb.Append(",");
                sb.Append("\"jyasm_extracted_dir\":\"").Append(JsonEscape(record.JyasmExtractedDir)).Append("\"");
                first = false;
            }
            if (record.JyasmImageUrls.Count > 0)
            {
                if (!first) sb.Append(",");
                sb.Append("\"jyasm_image_urls\":").Append(StringArrayJson(record.JyasmImageUrls));
                first = false;
            }
            if (first)
            {
                return null;
            }
            sb.Append("}");
            return sb.ToString();
        }

        private static string BuildGbPropsJson(GbStandardPartInfo gb)
        {
            StringBuilder sb = new StringBuilder();
            sb.Append("{");
            bool first = true;
            if (!string.IsNullOrEmpty(gb.Gb))
            {
                sb.Append("\"gb\":\"").Append(JsonEscape(gb.Gb)).Append("\"");
                first = false;
            }
            if (!string.IsNullOrEmpty(gb.Meaning))
            {
                if (!first) sb.Append(",");
                sb.Append("\"meaning\":\"").Append(JsonEscape(gb.Meaning)).Append("\"");
                first = false;
            }
            if (!string.IsNullOrEmpty(gb.Spec))
            {
                if (!first) sb.Append(",");
                sb.Append("\"spec\":\"").Append(JsonEscape(gb.Spec)).Append("\"");
                first = false;
            }
            if (!string.IsNullOrEmpty(gb.Material))
            {
                if (!first) sb.Append(",");
                sb.Append("\"material\":\"").Append(JsonEscape(gb.Material)).Append("\"");
                first = false;
            }
            if (first)
            {
                return null;
            }
            sb.Append("}");
            return sb.ToString();
        }

        private static JiavuDbConfig LoadConfig(string path)
        {
            if (!File.Exists(path))
            {
                throw new FileNotFoundException("未找到配置文件：" + path);
            }
            string json = File.ReadAllText(path, Utf8NoBom);
            JiavuDbConfig config = new JiavuDbConfig();
            config.Host = JsonValue(json, "jiavu_db_host").Trim();
            string port = JsonValue(json, "jiavu_db_port").Trim();
            int parsedPort;
            if (!string.IsNullOrEmpty(port) && int.TryParse(port, out parsedPort))
            {
                config.Port = parsedPort;
            }
            config.Database = JsonValue(json, "jiavu_db_name").Trim();
            config.User = JsonValue(json, "jiavu_db_user").Trim();
            config.Password = JsonValue(json, "jiavu_db_password");
            config.DifyBaseUrl = JsonValue(json, "dify_sync_base_url").Trim();
            config.DifyDatasetApiKey = JsonValue(json, "dify_dataset_api_key").Trim();
            config.DifyDatasetId = JsonValue(json, "dify_dataset_id").Trim();
            config.DifyKnowledgeGraphId = JsonValue(json, "dify_knowledge_graph_id").Trim();
            config.DifyDatasetIndexingTechnique = JsonValue(json, "dify_dataset_indexing_technique").Trim();
            if (string.IsNullOrWhiteSpace(config.DifyDatasetIndexingTechnique))
            {
                config.DifyDatasetIndexingTechnique = "economy";
            }
            config.DifyKnowledgeBaseDocumentPrefix = JsonValue(json, "dify_knowledge_base_document_name").Trim();
            config.DifyKnowledgeBaseDocumentPrefix =
                NormalizeKnowledgeBaseDocumentPrefix(config.DifyKnowledgeBaseDocumentPrefix);

            config.JyasmServiceBaseUrl = JsonValue(json, "jyasm_service_base_url").Trim();
            config.JyasmServiceToken = JsonValue(json, "jyasm_service_token").Trim();
            config.JyasmServicePublicBaseUrl = JsonValue(json, "jyasm_service_public_base_url").Trim();
            string triggerRaw = JsonValue(json, "jyasm_service_trigger_refresh").Trim();
            if (triggerRaw.Length > 0)
            {
                bool triggerVal;
                if (bool.TryParse(triggerRaw, out triggerVal))
                {
                    config.JyasmServiceTriggerRefresh = triggerVal;
                }
                else if (triggerRaw == "1")
                {
                    config.JyasmServiceTriggerRefresh = true;
                }
                else if (triggerRaw == "0")
                {
                    config.JyasmServiceTriggerRefresh = false;
                }
            }
            string timeoutRaw = JsonValue(json, "jyasm_service_timeout_ms").Trim();
            int timeoutVal;
            if (timeoutRaw.Length > 0 && int.TryParse(timeoutRaw, out timeoutVal) && timeoutVal > 0)
            {
                config.JyasmServiceTimeoutMs = timeoutVal;
            }
            return config;
        }

        private static void ValidateConfig(JiavuDbConfig config)
        {
            if (string.IsNullOrWhiteSpace(config.Host))
            {
                throw new InvalidOperationException("请先在配置文件或设置中填写佳鱼数据库地址。");
            }
            if (config.Port <= 0 || config.Port > 65535)
            {
                throw new InvalidOperationException("佳鱼数据库端口无效。");
            }
            if (string.IsNullOrWhiteSpace(config.User))
            {
                throw new InvalidOperationException("请先在配置文件或设置中填写佳鱼数据库用户名。");
            }
        }

        private static string GetArg(string[] args, string name, string fallback)
        {
            for (int i = 0; i + 1 < args.Length; ++i)
            {
                if (string.Equals(args[i], name, StringComparison.OrdinalIgnoreCase))
                {
                    return args[i + 1];
                }
            }
            return fallback;
        }

        private static string JsonValue(string json, string key)
        {
            Match match = Regex.Match(json, "\"" + Regex.Escape(key) + "\"\\s*:\\s*\"(?<value>(?:\\\\.|[^\"])*)\"");
            return match.Success ? JsonUnescape(match.Groups["value"].Value) : "";
        }

        private static string JsonEscape(string value)
        {
            if (value == null)
            {
                return "";
            }
            StringBuilder builder = new StringBuilder();
            foreach (char ch in value)
            {
                switch (ch)
                {
                    case '\\':
                        builder.Append("\\\\");
                        break;
                    case '"':
                        builder.Append("\\\"");
                        break;
                    case '\r':
                        builder.Append("\\r");
                        break;
                    case '\n':
                        builder.Append("\\n");
                        break;
                    case '\t':
                        builder.Append("\\t");
                        break;
                    default:
                        builder.Append(ch);
                        break;
                }
            }
            return builder.ToString();
        }

        private static string JsonUnescape(string value)
        {
            return value.Replace("\\\"", "\"")
                .Replace("\\\\", "\\")
                .Replace("\\r", "\r")
                .Replace("\\n", "\n")
                .Replace("\\t", "\t");
        }

        private static string SqlEscape(string value)
        {
            return value.Replace("\\", "\\\\").Replace("'", "''");
        }

        private static string QuoteIdentifier(string value)
        {
            return "`" + value.Replace("`", "``") + "`";
        }

        private static bool IsBinaryType(string dataType)
        {
            string type = Normalize(dataType);
            return ContainsAny(type, "blob", "binary", "varbinary", "geometry", "point", "linestring", "polygon");
        }

        private static bool ContainsAny(string value, params string[] needles)
        {
            foreach (string needle in needles)
            {
                if (value.IndexOf(needle, StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    return true;
                }
            }
            return false;
        }

        private static string Normalize(string value)
        {
            return (value ?? "").Replace("_", "").Replace("-", "").ToLowerInvariant();
        }

        private static string FlattenValues(Dictionary<string, string> values)
        {
            return string.Join("|", values.OrderBy(v => v.Key).Select(v => v.Key + "=" + v.Value).ToArray());
        }

        private static string FlattenParameters(List<ProductParameterInfo> parameters)
        {
            return string.Join(
                "|",
                parameters
                    .OrderBy(p => p.Sort)
                    .ThenBy(p => p.Name, StringComparer.OrdinalIgnoreCase)
                    .Select(p => p.Id + "=" + p.Name + ":" + p.FieldId + ":" + p.FieldName +
                        ":" + p.Unit + ":" + p.Range + ":" + p.Remark + ":" + p.Default + ":" + p.StepPage +
                        ":" + p.Image)
                    .ToArray());
        }

        private static string FlattenGbParts(List<GbStandardPartInfo> parts)
        {
            if (parts == null || parts.Count == 0)
            {
                return "";
            }
            return string.Join(
                "|",
                parts
                    .OrderBy(p => p.Gb, StringComparer.OrdinalIgnoreCase)
                    .ThenBy(p => p.Spec, StringComparer.OrdinalIgnoreCase)
                    .ThenBy(p => p.Material, StringComparer.OrdinalIgnoreCase)
                    .Select(p => p.Gb + ":" + p.Spec + ":" + p.Material)
                    .ToArray());
        }

        private static string GetValue(Dictionary<string, string> values, params string[] keys)
        {
            foreach (string key in keys)
            {
                if (values.ContainsKey(key))
                {
                    return values[key] ?? "";
                }
            }
            return "";
        }

        private static bool IsVisible(Dictionary<string, string> values)
        {
            string visible = GetValue(values, "visible", "isVisible", "enabled", "status").Trim();
            if (visible.Length == 0)
            {
                return true;
            }
            return !(string.Equals(visible, "0", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(visible, "false", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(visible, "disabled", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(visible, "delete", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(visible, "deleted", StringComparison.OrdinalIgnoreCase));
        }

        private static bool LooksLikeProductModel(string name, string path)
        {
            string value = (name ?? "") + " " + (path ?? "");
            return ContainsAny(value, ".jyasm", ".jypar", ".zw3", ".z3", ".asm");
        }

        private static int ParseInt(string value, int fallback)
        {
            int parsed;
            return int.TryParse(value, out parsed) ? parsed : fallback;
        }

        private static List<string> SplitTreeNode(string treeNode)
        {
            return (treeNode ?? "")
                .Split(new char[] { ',' }, StringSplitOptions.RemoveEmptyEntries)
                .Select(p => p.Trim())
                .Where(p => p.Length > 0)
                .ToList();
        }

        private static List<string> SplitPath(string path)
        {
            return (path ?? "")
                .Replace("\\", "/")
                .Split(new char[] { '/' }, StringSplitOptions.RemoveEmptyEntries)
                .Select(p => p.Trim())
                .Where(p => p.Length > 0)
                .ToList();
        }

        private static string DirectoryPathFromModelPath(string path)
        {
            List<string> parts = SplitPath(path);
            if (parts.Count == 0)
            {
                return "";
            }
            if (LooksLikeProductModel(parts[parts.Count - 1], parts[parts.Count - 1]))
            {
                parts.RemoveAt(parts.Count - 1);
            }
            return string.Join("/", parts.ToArray());
        }

        private static string CombinePath(string left, string right)
        {
            if (string.IsNullOrWhiteSpace(left))
            {
                return right ?? "";
            }
            if (string.IsNullOrWhiteSpace(right))
            {
                return left ?? "";
            }
            return left.TrimEnd('/', '\\') + "/" + right.TrimStart('/', '\\');
        }

        private static string HashHex(string value)
        {
            SHA256 sha = SHA256.Create();
            byte[] hash = sha.ComputeHash(Encoding.UTF8.GetBytes(value ?? ""));
            StringBuilder builder = new StringBuilder(hash.Length * 2);
            foreach (byte item in hash)
            {
                builder.Append(item.ToString("x2"));
            }
            return builder.ToString();
        }

        private static string Truncate(string value, int maxLength)
        {
            if (string.IsNullOrEmpty(value) || value.Length <= maxLength)
            {
                return value ?? "";
            }
            return value.Substring(0, maxLength);
        }
    }

    internal sealed class DeltaInfo
    {
        public int Added;
        public int Updated;
        public int Removed;
        public int Unchanged;
        public List<string> AddedKeys = new List<string>();
        public List<string> UpdatedKeys = new List<string>();
        public List<string> RemovedKeys = new List<string>();
    }

    internal sealed class DifySyncResult
    {
        public bool Ok;
        public bool Skipped;
        public bool KnowledgeBaseSynced;
        public bool KnowledgeGraphSynced;
        public int KnowledgeBaseDocumentCount;
        public int KnowledgeBaseStatusCode;
        public int KnowledgeGraphStatusCode;
        public string Error = "";
    }

    internal sealed class DifyDocumentEntry
    {
        public string Name = "";
        public string Text = "";
    }

    internal sealed class DifyDocumentRef
    {
        public string Id = "";
        public string Name = "";
    }

    internal sealed class HttpResult
    {
        public bool Ok;
        public int StatusCode;
        public string Body = "";
    }

    internal sealed class SyncReport
    {
        public bool Ok;
        public string Message = "";
        public string Database = "";
        public int RecordCount;
        public int Added;
        public int Updated;
        public int Removed;
        public int Unchanged;
        public string KnowledgeBasePath = "";
        public string GraphPath = "";
        public string ManifestPath = "";
        public string DeltaPath = "";
        public bool DifyKnowledgeBaseSynced;
        public bool DifyKnowledgeGraphSynced;
        public int DifyKnowledgeBaseStatusCode;
        public int DifyKnowledgeGraphStatusCode;
        public string DifyError = "";
        public List<string> Warnings = new List<string>();

        public string ToJson()
        {
            return "{\"ok\":" + (Ok ? "true" : "false") +
                ",\"message\":\"" + ProgramJsonEscape(Message) +
                "\",\"database\":\"" + ProgramJsonEscape(Database) +
                "\",\"record_count\":" + RecordCount +
                ",\"added\":" + Added +
                ",\"updated\":" + Updated +
                ",\"removed\":" + Removed +
                ",\"unchanged\":" + Unchanged +
                ",\"knowledge_base_path\":\"" + ProgramJsonEscape(KnowledgeBasePath) +
                "\",\"graph_path\":\"" + ProgramJsonEscape(GraphPath) +
                "\",\"manifest_path\":\"" + ProgramJsonEscape(ManifestPath) +
                "\",\"delta_path\":\"" + ProgramJsonEscape(DeltaPath) +
                "\",\"dify_knowledge_base_synced\":" + (DifyKnowledgeBaseSynced ? "true" : "false") +
                ",\"dify_knowledge_graph_synced\":" + (DifyKnowledgeGraphSynced ? "true" : "false") +
                ",\"dify_knowledge_base_status_code\":" + DifyKnowledgeBaseStatusCode +
                ",\"dify_knowledge_graph_status_code\":" + DifyKnowledgeGraphStatusCode +
                ",\"dify_error\":\"" + ProgramJsonEscape(DifyError) +
                "\",\"warnings\":[" + string.Join(",", Warnings.Select(w => "\"" + ProgramJsonEscape(w) + "\"").ToArray()) + "]}";
        }

        private static string ProgramJsonEscape(string value)
        {
            if (value == null)
            {
                return "";
            }
            return value.Replace("\\", "\\\\")
                .Replace("\"", "\\\"")
                .Replace("\r", "\\r")
                .Replace("\n", "\\n")
                .Replace("\t", "\\t");
        }
    }
}
