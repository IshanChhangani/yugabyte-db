/*
 * Copyright (c) YugaByte, Inc.
 */

package update

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// updateGCSStorageConfigurationCmd represents the storage config command
var updateGCSStorageConfigurationCmd = &cobra.Command{
	Use:   "gcs",
	Short: "Update an GCS YugabyteDB Anywhere storage configuration",
	Long:  "Update an GCS storage configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		storageNameFlag, err := cmd.Flags().GetString("storage-config-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(storageNameFlag)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No storage configuration name found to update\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, err := ybaAuthClient.NewAuthAPIClient()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		authAPI.GetCustomerUUID()
		storageConfigListRequest := authAPI.GetListOfCustomerConfig()

		r, response, err := storageConfigListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Storage Configuration", "Update - List Customer Configurations")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		storageConfigs := make([]ybaclient.CustomerConfigUI, 0)
		for _, s := range r {
			if strings.Compare(s.GetType(), util.StorageCustomerConfigType) == 0 {
				storageConfigs = append(storageConfigs, s)
			}
		}

		// filter by name and/or by storage-configurations code
		storageName, err := cmd.Flags().GetString("storage-config-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if storageName != "" {
			storageConfigsName := make([]ybaclient.CustomerConfigUI, 0)
			for _, s := range storageConfigs {
				if strings.Compare(s.GetConfigName(), storageName) == 0 {
					storageConfigsName = append(storageConfigsName, s)
				}
			}
			storageConfigs = storageConfigsName
		}

		r = storageConfigs

		if len(r) < 1 {
			fmt.Println("No storage configurations found")
			return
		}

		var storageConfig ybaclient.CustomerConfigUI
		if len(r) > 0 {
			storageConfig = r[0]
		}

		storageUUID := storageConfig.GetConfigUUID()

		storageCode := "GCS"

		newStorageName, err := cmd.Flags().GetString("update-storage-config-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(newStorageName) > 0 {
			storageName = newStorageName
		}

		if strings.Compare(storageConfig.GetName(), storageCode) != 0 {
			err := fmt.Sprintf("Incorrect command to edit selected storage configuration."+
				" Use %s command.", strings.ToLower(storageConfig.GetName()))
			logrus.Fatalf(formatter.Colorize(err+"\n", formatter.RedColor))
		}

		data := storageConfig.GetData()

		updateCredentials, err := cmd.Flags().GetBool("update-credentials")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if updateCredentials {
			isIAM, err := cmd.Flags().GetBool("use-gcp-iam")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if isIAM {
				data[util.UseGCPIAM] = strconv.FormatBool(isIAM)
			} else {
				gcsFilePath, err := cmd.Flags().GetString("gcs-credentials-file-path")
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				var gcsCreds string
				if len(gcsFilePath) == 0 {
					gcsCreds, err = util.GcpGetCredentialsAsString()
					if err != nil {
						logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
					}

				} else {
					gcsCreds, err = util.GcpGetCredentialsAsStringFromFilePath(gcsFilePath)
					if err != nil {
						logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
					}
				}
				data[util.GCSCredentialsJSON] = gcsCreds
			}
		}

		requestBody := ybaclient.CustomerConfig{
			Name:         storageCode,
			CustomerUUID: authAPI.CustomerUUID,
			ConfigName:   storageName,
			Type:         util.StorageCustomerConfigType,
			Data:         data,
		}

		_, response, err = authAPI.EditCustomerConfig(storageUUID).
			Config(requestBody).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Storage Configuration", "Update GCS")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		fmt.Printf("The storage configuration %s (%s) has been updated\n",
			formatter.Colorize(storageName, formatter.GreenColor), storageUUID)

		updateStorageConfigurationUtil(authAPI, storageName, storageUUID)

	},
}

func init() {
	updateGCSStorageConfigurationCmd.Flags().SortFlags = false

	// Flags needed for GCS
	updateGCSStorageConfigurationCmd.Flags().Bool("update-credentials", false,
		"[Optional] Update credentials of the storage configuration, defaults to false."+
			" If set to true, provide either gcs-credentials-file-path"+
			" or set use-gcp-iam.")
	updateGCSStorageConfigurationCmd.Flags().String("gcs-credentials-file-path", "",
		fmt.Sprintf("GCS Credentials File Path. %s "+
			"Can also be set using environment variable %s.",
			formatter.Colorize(
				"Required for non IAM role based storage configurations.",
				formatter.GreenColor),
			util.GCPCredentialsEnv))
	updateGCSStorageConfigurationCmd.Flags().Bool("use-gcp-iam", false,
		"[Optional] Use IAM Role from the YugabyteDB Anywhere Host. "+
			"Supported for Kubernetes GKE clusters with workload identity. Configuration "+
			"creation will fail on insufficient permissions on the host, defaults to false.")

}
